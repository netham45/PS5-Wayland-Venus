/* A queue submission and CPU readback, independent of WSI and VideoOut. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "cube_functions.h"
#include "ps5log.h"
extern int ps5wl_vulkan_install(void);
static PFN_vkCmdFillBuffer test_vkCmdFillBuffer;
static PFN_vkInvalidateMappedMemoryRanges test_vkInvalidateMappedMemoryRanges;

#define VK_CHECK(expr) do { VkResult r = (expr); if (r != VK_SUCCESS) { \
    ps5log("FAIL %s result=%d", #expr, r); goto fail; } } while (0)

int main(void) {
    VkInstance instance = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    int status = 1;
    ps5log_init("ps5wl-vktest", 9591);
    ps5log("starting RADV GPU smoke (no surface, no VideoOut)");
    if (ps5wl_vulkan_install() || load_vulkan_library() != VK_SUCCESS) {
        ps5log("FAIL loading RADV library");
        goto fail;
    }
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "ps5wl-vktest", .apiVersion = VK_API_VERSION_1_1};
    VkInstanceCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app};
    VK_CHECK(vkCreateInstance(&ici, NULL, &instance));
    load_vulkan_instance_functions(instance);
    uint32_t count = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &count, NULL));
    ps5log("physical devices=%u", count);
    if (!count) goto fail;
    VkPhysicalDevice physical;
    count = 1;
    VkResult result = vkEnumeratePhysicalDevices(instance, &count, &physical);
    if (result != VK_SUCCESS && result != VK_INCOMPLETE) goto fail;
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physical, &props);
    ps5log("device=%s vendor=%04x device=%04x api=%u.%u.%u", props.deviceName,
        props.vendorID, props.deviceID, VK_API_VERSION_MAJOR(props.apiVersion),
        VK_API_VERSION_MINOR(props.apiVersion), VK_API_VERSION_PATCH(props.apiVersion));
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, NULL);
    VkQueueFamilyProperties *families = calloc(count, sizeof(*families));
    if (!families) goto fail;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families);
    uint32_t family = UINT32_MAX;
    for (uint32_t i = 0; i < count; i++)
        if (families[i].queueCount && (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) { family = i; break; }
    free(families);
    if (family == UINT32_MAX) goto fail;
    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = family, .queueCount = 1, .pQueuePriorities = &priority};
    VkDeviceCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci};
    VK_CHECK(vkCreateDevice(physical, &dci, NULL, &device));
    load_vulkan_device_functions(device);
    test_vkCmdFillBuffer = (PFN_vkCmdFillBuffer)vkGetDeviceProcAddr(device, "vkCmdFillBuffer");
    test_vkInvalidateMappedMemoryRanges = (PFN_vkInvalidateMappedMemoryRanges)
        vkGetDeviceProcAddr(device, "vkInvalidateMappedMemoryRanges");
    if (!test_vkCmdFillBuffer || !test_vkInvalidateMappedMemoryRanges) goto fail;
    VkQueue queue;
    vkGetDeviceQueue(device, family, 0, &queue);
    VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = 65536, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    VK_CHECK(vkCreateBuffer(device, &bci, NULL, &buffer));
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device, buffer, &req);
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(physical, &mp);
    uint32_t type = UINT32_MAX;
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
        ps5log("memory type=%u flags=%x heap=%u", i, mp.memoryTypes[i].propertyFlags, mp.memoryTypes[i].heapIndex);
        if ((req.memoryTypeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
            type = i;
    }
    if (type == UINT32_MAX) goto fail;
    VkMemoryAllocateInfo mai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size, .memoryTypeIndex = type};
    VK_CHECK(vkAllocateMemory(device, &mai, NULL, &memory));
    VK_CHECK(vkBindBufferMemory(device, buffer, memory, 0));
    VkCommandPoolCreateInfo pci = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = family};
    VK_CHECK(vkCreateCommandPool(device, &pci, NULL, &pool));
    VkCommandBuffer command;
    VkCommandBufferAllocateInfo cai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
    VK_CHECK(vkAllocateCommandBuffers(device, &cai, &command));
    VkCommandBufferBeginInfo cbi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    VK_CHECK(vkBeginCommandBuffer(command, &cbi));
    test_vkCmdFillBuffer(command, buffer, 0, 65536, 0x1a2b3c4d);
    VkMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
        0, 1, &barrier, 0, NULL, 0, NULL);
    VK_CHECK(vkEndCommandBuffer(command));
    VkFenceCreateInfo fci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VK_CHECK(vkCreateFence(device, &fci, NULL, &fence));
    VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1, .pCommandBuffers = &command};
    VK_CHECK(vkQueueSubmit(queue, 1, &submit, fence));
    VK_CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, 10000000000ull));
    uint32_t *map;
    VK_CHECK(vkMapMemory(device, memory, 0, VK_WHOLE_SIZE, 0, (void **)&map));
    VkMappedMemoryRange range = {.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
        .memory = memory, .size = VK_WHOLE_SIZE};
    VK_CHECK(test_vkInvalidateMappedMemoryRanges(device, 1, &range));
    unsigned bad = 0;
    for (unsigned i = 0; i < 65536 / sizeof(*map); i++) if (map[i] != 0x1a2b3c4d) bad++;
    ps5log("GPU readback words=16384 expected=1a2b3c4d first=%08x mismatches=%u", map[0], bad);
    vkUnmapMemory(device, memory);
    if (bad) goto fail;
    status = 0;
fail:
    if (device) {
        vkDeviceWaitIdle(device);
        if (fence) vkDestroyFence(device, fence, NULL);
        if (pool) vkDestroyCommandPool(device, pool, NULL);
        if (buffer) vkDestroyBuffer(device, buffer, NULL);
        if (memory) vkFreeMemory(device, memory, NULL);
        vkDestroyDevice(device, NULL);
    }
    if (instance) vkDestroyInstance(instance, NULL);
    ps5log("RESULT %s", status ? "FAIL" : "PASS");
    ps5log_flush_wait(30000);
    return status;
}
