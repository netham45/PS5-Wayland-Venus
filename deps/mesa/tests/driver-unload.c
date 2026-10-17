/* SPDX-License-Identifier: MIT
 * Run under LeakSanitizer to cover ICD-owned allocations across dlclose.
 * No Vulkan loader, GPU server, device or queue is needed: lavapipe screen
 * creation during physical-device enumeration exercises CPU topology setup.
 */
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s ICD-LIBRARY\n", argv[0]);
        return 2;
    }
    for (unsigned iteration = 0; iteration < 4; ++iteration) {
        void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
        if (!library) {
            fprintf(stderr, "dlopen: %s\n", dlerror());
            return 1;
        }
        PFN_vkGetInstanceProcAddr get =
            (PFN_vkGetInstanceProcAddr)dlsym(library, "vk_icdGetInstanceProcAddr");
        if (!get) {
            fprintf(stderr, "missing ICD resolver\n");
            dlclose(library);
            return 1;
        }
        PFN_vkCreateInstance create = (PFN_vkCreateInstance)get(VK_NULL_HANDLE, "vkCreateInstance");
        VkInstanceCreateInfo info = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        VkInstance instance = VK_NULL_HANDLE;
        VkResult result = create ? create(&info, NULL, &instance) : VK_ERROR_INITIALIZATION_FAILED;
        if (result != VK_SUCCESS) {
            fprintf(stderr, "vkCreateInstance: %d\n", result);
            dlclose(library);
            return 1;
        }
        PFN_vkEnumeratePhysicalDevices enumerate =
            (PFN_vkEnumeratePhysicalDevices)get(instance, "vkEnumeratePhysicalDevices");
        PFN_vkDestroyInstance destroy = (PFN_vkDestroyInstance)get(instance, "vkDestroyInstance");
        uint32_t count = 0;
        result = enumerate(instance, &count, NULL);
        destroy(instance, NULL);
        int close_result = dlclose(library);
        if (result != VK_SUCCESS || !count || close_result) {
            fprintf(stderr, "enumerate: %d devices=%u dlclose=%d\n", result, count, close_result);
            return 1;
        }
        printf("ICD unload iteration=%u devices=%u\n", iteration + 1, count);
    }
    return 0;
}
