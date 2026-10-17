#define _GNU_SOURCE 1
#define VK_NO_PROTOTYPES
#include "venus-protocol/vulkan.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "pending-client:%d: %s\n", __LINE__, #x); return 1; } } while (0)
#define GLOBAL(name) PFN_vk##name name = (PFN_vk##name)get(NULL, "vk" #name); CHECK(name)
#define INSTANCE(name) PFN_vk##name name = (PFN_vk##name)get(instance, "vk" #name); CHECK(name)
#define DEVICE(name) PFN_vk##name name = (PFN_vk##name)GetDeviceProcAddr(device, "vk" #name); CHECK(name)

/* A valid timeline wait may precede its signal. The killed variant deliberately
 * never supplies that signal: server EOF must preserve GPU-referenced pages. */
int main(int argc, char **argv)
{
    CHECK(argc == 2 && (!strcmp(argv[1], "pending") || !strcmp(argv[1], "idle")));
    void *library = dlopen(getenv("PS5WL_VULKAN_LIBRARY"), RTLD_NOW | RTLD_LOCAL);
    CHECK(library);
    PFN_vkGetInstanceProcAddr get = (PFN_vkGetInstanceProcAddr)dlsym(library, "vk_icdGetInstanceProcAddr");
    if (!get) get = (PFN_vkGetInstanceProcAddr)dlsym(library, "vkGetInstanceProcAddr");
    CHECK(get);
    GLOBAL(CreateInstance);
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_2};
    VkInstanceCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app};
    VkInstance instance;
    CHECK(CreateInstance(&ici, NULL, &instance) == VK_SUCCESS);
    INSTANCE(EnumeratePhysicalDevices);
    INSTANCE(GetPhysicalDeviceQueueFamilyProperties);
    INSTANCE(GetPhysicalDeviceMemoryProperties);
    INSTANCE(CreateDevice);
    INSTANCE(GetDeviceProcAddr);
    uint32_t count = 1;
    VkPhysicalDevice physical;
    VkResult result = EnumeratePhysicalDevices(instance, &count, &physical);
    CHECK((result == VK_SUCCESS || result == VK_INCOMPLETE) && count);
    GetPhysicalDeviceQueueFamilyProperties(physical, &count, NULL);
    VkQueueFamilyProperties *families = calloc(count, sizeof(*families));
    CHECK(families);
    GetPhysicalDeviceQueueFamilyProperties(physical, &count, families);
    uint32_t family = UINT32_MAX;
    for (uint32_t i = 0; i < count; i++)
        if (families[i].queueCount && (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) { family = i; break; }
    free(families);
    CHECK(family != UINT32_MAX);
    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = family, .queueCount = 1, .pQueuePriorities = &priority};
    VkPhysicalDeviceTimelineSemaphoreFeatures timeline = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES, .timelineSemaphore = VK_TRUE};
    VkDeviceCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .pNext = &timeline,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci};
    VkDevice device;
    CHECK(CreateDevice(physical, &dci, NULL, &device) == VK_SUCCESS);
    DEVICE(GetDeviceQueue); DEVICE(CreateBuffer); DEVICE(GetBufferMemoryRequirements);
    DEVICE(AllocateMemory); DEVICE(BindBufferMemory); DEVICE(MapMemory);
    DEVICE(CreateCommandPool); DEVICE(AllocateCommandBuffers); DEVICE(BeginCommandBuffer);
    DEVICE(CmdFillBuffer); DEVICE(EndCommandBuffer); DEVICE(CreateSemaphore);
    DEVICE(CreateFence); DEVICE(QueueSubmit); DEVICE(WaitForFences); DEVICE(SignalSemaphore);
    VkQueue queue;
    GetDeviceQueue(device, family, 0, &queue);
    VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = 16384,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    VkBuffer buffer;
    CHECK(CreateBuffer(device, &bci, NULL, &buffer) == VK_SUCCESS);
    VkMemoryRequirements req;
    GetBufferMemoryRequirements(device, buffer, &req);
    VkPhysicalDeviceMemoryProperties props;
    GetPhysicalDeviceMemoryProperties(physical, &props);
    uint32_t type = UINT32_MAX;
    for (uint32_t i = 0; i < props.memoryTypeCount; i++)
        if ((req.memoryTypeBits & (1u << i)) && (props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) { type = i; break; }
    CHECK(type != UINT32_MAX);
    VkMemoryAllocateInfo mai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size, .memoryTypeIndex = type};
    VkDeviceMemory memory;
    CHECK(AllocateMemory(device, &mai, NULL, &memory) == VK_SUCCESS);
    CHECK(BindBufferMemory(device, buffer, memory, 0) == VK_SUCCESS);
    void *mapped;
    CHECK(MapMemory(device, memory, 0, req.size, 0, &mapped) == VK_SUCCESS);
    memset(mapped, 0, req.size);
    VkCommandPoolCreateInfo pci = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = family};
    VkCommandPool pool;
    CHECK(CreateCommandPool(device, &pci, NULL, &pool) == VK_SUCCESS);
    VkCommandBufferAllocateInfo cai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
    VkCommandBuffer command;
    CHECK(AllocateCommandBuffers(device, &cai, &command) == VK_SUCCESS);
    VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    CHECK(BeginCommandBuffer(command, &begin) == VK_SUCCESS);
    CmdFillBuffer(command, buffer, 0, 16384, 0x12345678);
    CHECK(EndCommandBuffer(command) == VK_SUCCESS);
    VkSemaphoreTypeCreateInfo sti = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
        .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE, .initialValue = 0};
    VkSemaphoreCreateInfo sci = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = &sti};
    VkSemaphore semaphore;
    CHECK(CreateSemaphore(device, &sci, NULL, &semaphore) == VK_SUCCESS);
    VkFenceCreateInfo fci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence;
    CHECK(CreateFence(device, &fci, NULL, &fence) == VK_SUCCESS);
    uint64_t value = 1;
    VkTimelineSemaphoreSubmitInfo tsi = {.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
        .waitSemaphoreValueCount = 1, .pWaitSemaphoreValues = &value};
    VkPipelineStageFlags stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .pNext = &tsi,
        .waitSemaphoreCount = 1, .pWaitSemaphores = &semaphore, .pWaitDstStageMask = &stage,
        .commandBufferCount = 1, .pCommandBuffers = &command};
    CHECK(QueueSubmit(queue, 1, &submit, fence) == VK_SUCCESS);
    CHECK(WaitForFences(device, 1, &fence, VK_TRUE, UINT64_C(10000000)) == VK_TIMEOUT);
    if (!strcmp(argv[1], "idle")) {
        VkSemaphoreSignalInfo signal = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO,
            .semaphore = semaphore, .value = 1};
        CHECK(SignalSemaphore(device, &signal) == VK_SUCCESS);
        CHECK(WaitForFences(device, 1, &fence, VK_TRUE, UINT64_C(2000000000)) == VK_SUCCESS);
        puts("idle submission completed");
    } else {
        puts("pending submission waits on unsignalled timeline");
    }
    fflush(stdout);
    for (;;) pause();
}
