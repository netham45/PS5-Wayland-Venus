#include "memory_mask.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    VkPhysicalDeviceMemoryProperties p = {.memoryTypeCount = 3};
    for (unsigned i = 0; i < 3; i++) {
        p.memoryTypes[i].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        p.memoryTypes[i].heapIndex = i;
    }
    VkMemoryPropertyFlags original = p.memoryTypes[0].propertyFlags;
    gpud_mask_memory_types(&p, 1);
    assert(p.memoryTypes[0].propertyFlags == original);
    assert(p.memoryTypes[1].propertyFlags == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    assert(p.memoryTypes[2].propertyFlags == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    assert(p.memoryTypes[1].heapIndex == 1);
    gpud_mask_memory_types(&p, 0);
    assert(p.memoryTypes[0].propertyFlags == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    puts("memory-mask: importable first type preserved; fake second type hidden PASS");
    return 0;
}
