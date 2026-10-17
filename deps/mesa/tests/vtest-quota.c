/* SPDX-License-Identifier: MIT
 * Run against gpud-host --ps5-limits: a rejected allocation must return OOM,
 * not a successful phantom handle that kills the context on its next use.
 */
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdbool.h>

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
    PFN_vkGetInstanceProcAddr get = (PFN_vkGetInstanceProcAddr)dlsym(library, "vk_icdGetInstanceProcAddr");
    if (!get) return 1;
    PFN_vkCreateInstance create = (PFN_vkCreateInstance)get(VK_NULL_HANDLE, "vkCreateInstance");
    VkInstanceCreateInfo instance_info = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    VkInstance instance;
    if (create(&instance_info, NULL, &instance) != VK_SUCCESS) return 1;
#define INSTANCE(name) PFN_vk##name name = (PFN_vk##name)get(instance, "vk" #name)
    INSTANCE(EnumeratePhysicalDevices);
    INSTANCE(GetPhysicalDeviceMemoryProperties);
    INSTANCE(GetPhysicalDeviceQueueFamilyProperties);
    INSTANCE(CreateDevice);
    INSTANCE(GetDeviceProcAddr);
    INSTANCE(DestroyInstance);
    VkPhysicalDevice physical;
    uint32_t count = 1;
    if (EnumeratePhysicalDevices(instance, &count, &physical) != VK_SUCCESS || !count) return 1;
    VkPhysicalDeviceMemoryProperties props;
    GetPhysicalDeviceMemoryProperties(physical, &props);
    uint32_t type;
    for (type = 0; type < props.memoryTypeCount; type++)
        if (props.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) break;
    if (type == props.memoryTypeCount) return 1;
    uint32_t family_count = 0;
    GetPhysicalDeviceQueueFamilyProperties(physical, &family_count, NULL);
    if (!family_count) return 1;
    float priority = 1;
    VkDeviceQueueCreateInfo queue = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                    .queueFamilyIndex = 0, .queueCount = 1, .pQueuePriorities = &priority};
    VkDeviceCreateInfo device_info = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                                     .queueCreateInfoCount = 1, .pQueueCreateInfos = &queue};
    VkDevice device;
    if (CreateDevice(physical, &device_info, NULL, &device) != VK_SUCCESS) return 1;
#define DEVICE(name) PFN_vk##name name = (PFN_vk##name)GetDeviceProcAddr(device, "vk" #name)
    DEVICE(AllocateMemory);
    DEVICE(FreeMemory);
    DEVICE(DestroyDevice);
    VkMemoryAllocateInfo alloc = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                 .allocationSize = 80u * 1024u * 1024u, .memoryTypeIndex = type};
    VkDeviceMemory first = VK_NULL_HANDLE, second = VK_NULL_HANDLE;
    VkResult one = AllocateMemory(device, &alloc, NULL, &first);
    VkResult two = AllocateMemory(device, &alloc, NULL, &second);
    printf("quota allocations first=%d second=%d\n", one, two);
    if (second) FreeMemory(device, second, NULL);
    if (first) FreeMemory(device, first, NULL);
    bool oom = two == VK_ERROR_OUT_OF_HOST_MEMORY || two == VK_ERROR_OUT_OF_DEVICE_MEMORY;
    VkDeviceMemory recovered = VK_NULL_HANDLE;
    VkResult three = AllocateMemory(device, &alloc, NULL, &recovered);
    if (recovered) FreeMemory(device, recovered, NULL);
    DestroyDevice(device, NULL);
    DestroyInstance(instance, NULL);
    dlclose(library);
    printf("quota recovery=%d RESULT %s\n", three, one == VK_SUCCESS && oom && three == VK_SUCCESS ? "PASS" : "FAIL");
    return one == VK_SUCCESS && oom && three == VK_SUCCESS ? 0 : 1;
}
