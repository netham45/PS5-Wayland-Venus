#ifndef GPUD_MEMORY_MASK_H
#define GPUD_MEMORY_MASK_H
#include "venus-protocol/vulkan.h"
static inline void gpud_mask_memory_types(VkPhysicalDeviceMemoryProperties *p, uint32_t bits)
{
    for (uint32_t i = 0; i < p->memoryTypeCount; i++) {
        if (!(bits & (1u << i))) {
            p->memoryTypes[i].propertyFlags &= ~(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
            p->memoryTypes[i].propertyFlags |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        }
    }
}
#endif
