#ifndef GPUD_HOST_MEMORY_H
#define GPUD_HOST_MEMORY_H
#include "memory_mask.h"
#include "util/anon_file.h"
#include <unistd.h>
#include <sys/mman.h>
/* Query before advertising memory types, not after the client chooses a type.
 * The extension exposes the query on a logical device, so use a short-lived
 * device and an aligned probe mapping. No GPU submission is performed. */
static uint32_t gpud_query_host_types(struct vkr_physical_device *p)
{
    if (!p->EXT_external_memory_host)
        return 0;
    struct vn_physical_device_proc_table *vk = &p->proc_table;
    uint64_t align = MAX2(UINT64_C(16384), p->min_imported_host_pointer_alignment);
    if ((align & (align - 1)) || align > SIZE_MAX / 2)
        return 0;
    int fd = os_create_anonymous_file(align * 2, "vkr-type-probe");
    if (fd < 0)
        return 0;
    void *base = mmap(NULL, align * 2, PROT_READ | PROT_WRITE,
                      MAP_SHARED, fd, 0);
    close(fd);
    if (base == MAP_FAILED)
        return 0;
    void *ptr = (void *)(((uintptr_t)base + align - 1) & ~(uintptr_t)(align - 1));
    float priority = 0.0f;
    VkDeviceQueueCreateInfo queue = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = 0, .queueCount = 1, .pQueuePriorities = &priority };
    const char *ext = "VK_EXT_external_memory_host";
    VkDeviceCreateInfo info = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &queue,
        .enabledExtensionCount = 1, .ppEnabledExtensionNames = &ext };
    VkDevice dev;
    uint32_t bits = 0;
    if (vk->CreateDevice(p->base.handle.physical_device, &info, NULL, &dev) == VK_SUCCESS) {
        PFN_vkGetMemoryHostPointerPropertiesEXT query = (PFN_vkGetMemoryHostPointerPropertiesEXT)
            vk->GetDeviceProcAddr(dev, "vkGetMemoryHostPointerPropertiesEXT");
        PFN_vkDestroyDevice destroy = (PFN_vkDestroyDevice)vk->GetDeviceProcAddr(dev, "vkDestroyDevice");
        VkMemoryHostPointerPropertiesEXT props = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT };
        if (query && query(dev, VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,
                           ptr, &props) == VK_SUCCESS)
            bits = props.memoryTypeBits;
        destroy(dev, NULL);
    }
    munmap(base, align * 2);
    vkr_log("gpud host-pointer importable memory types: 0x%x", bits);
    return bits;
}
#endif
