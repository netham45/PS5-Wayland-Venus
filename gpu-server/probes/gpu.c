/* C2: bounded host-pointer import probe. No VideoOut or process creation. */
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include "ps5log.h"
#include "app.h"
#ifndef PROBE_BYTES
#define PROBE_BYTES 16384
#endif
#define PATTERN 0x1a2b3c4du
#define SOCKET_PATH "/system_tmp/ps5wl-gpu-probe"

#ifdef PROBE_APP
static void shell_exit(void) {
    int rc = sceSystemServiceLoadExec("exit", NULL);
    ps5log("sceSystemServiceLoadExec(exit) rc=0x%x", rc);
    for (;;) sleep(1);
}
static void *deadline(void *unused) {
    (void)unused;
    sleep(30);
    ps5log("STOP diagnostic deadline=30s; preserving potentially GPU-owned mappings");
    shell_exit();
    return NULL;
}
#endif

static int verify(const uint32_t *data, size_t bytes) {
    size_t bad = 0;
    for (size_t i = 0; i < bytes / 4; i++) bad += data[i] != PATTERN;
    ps5log("readback bytes=%zu words=%zu expected=%08x first=%08x mismatches=%zu", bytes, bytes / 4, PATTERN, data[0], bad);
    return bad != 0;
}

#ifdef PROBE_VERIFY
int main(void) {
    ps5log_init("ps5wl-gpu-c2-verifier", 9594);
    int status = 1;
    int socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    strcpy(address.sun_path, SOCKET_PATH);
    if (socket_fd < 0 || connect(socket_fd, (struct sockaddr *)&address, sizeof(address))) {
        ps5log("STOP verifier connect errno=%d", errno);
        goto done;
    }
    struct pollfd ready = {.fd = socket_fd, .events = POLLIN};
    if (poll(&ready, 1, 2000) != 1) { ps5log("STOP verifier receive timeout"); goto done; }
    size_t bytes = 0;
    struct iovec iov = {.iov_base = &bytes, .iov_len = sizeof(bytes)};
    union { struct cmsghdr alignment; unsigned char data[CMSG_SPACE(sizeof(int))]; } control;
    struct msghdr message = {.msg_iov = &iov, .msg_iovlen = 1,
        .msg_control = control.data, .msg_controllen = sizeof(control.data)};
    ssize_t count = recvmsg(socket_fd, &message, 0);
    struct cmsghdr *header = CMSG_FIRSTHDR(&message);
    if (count != sizeof(bytes) || (message.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) || !header ||
        header->cmsg_level != SOL_SOCKET || header->cmsg_type != SCM_RIGHTS ||
        header->cmsg_len != CMSG_LEN(sizeof(int)) ||
        (bytes != 16384 && bytes != (8u << 20) && bytes != (64u << 20))) {
        ps5log("STOP verifier malformed fd transfer count=%zd bytes=%zu", count, bytes);
        goto done;
    }
    int fd;
    memcpy(&fd, CMSG_DATA(header), sizeof(fd));
    void *map = mmap(NULL, bytes, PROT_READ, MAP_SHARED, fd, 0);
    ps5log("second process pid=%d SCM_RIGHTS fd=%d MAP_SHARED bytes=%zu ptr=%p errno=%d", getpid(), fd, bytes, map, errno);
    if (map != MAP_FAILED) {
        status = verify(map, bytes);
        munmap(map, bytes);
    }
    close(fd);
    char result = status ? 'F' : 'P';
    send(socket_fd, &result, 1, MSG_NOSIGNAL);
 done:
    if (socket_fd >= 0) close(socket_fd);
    ps5log("C2 cross-process RESULT %s code=%d", status ? "STOP" : "PASS", status);
    ps5log_flush_wait(5000);
    return status;
}
#else
#define FUNCTIONS(X) \
 X(CreateInstance) X(DestroyInstance) X(EnumeratePhysicalDevices) \
 X(GetPhysicalDeviceProperties2) X(GetPhysicalDeviceMemoryProperties) \
 X(GetPhysicalDeviceQueueFamilyProperties) X(CreateDevice) X(DestroyDevice) \
 X(GetDeviceProcAddr) X(GetDeviceQueue) X(CreateBuffer) X(DestroyBuffer) \
 X(GetBufferMemoryRequirements) X(AllocateMemory) X(FreeMemory) X(BindBufferMemory) \
 X(GetMemoryHostPointerPropertiesEXT) X(CreateCommandPool) X(DestroyCommandPool) \
 X(AllocateCommandBuffers) X(BeginCommandBuffer) X(EndCommandBuffer) \
 X(CmdFillBuffer) X(CmdPipelineBarrier) X(CreateFence) X(DestroyFence) \
 X(QueueSubmit) X(WaitForFences) X(CreateImage) X(DestroyImage) \
 X(GetImageMemoryRequirements) X(BindImageMemory) X(CmdClearColorImage) X(CmdCopyImageToBuffer)
#define DECLARE(name) static PFN_vk##name p##name;
FUNCTIONS(DECLARE)
#define CHECK(call) do { VkResult r = (call); ps5log("%s result=%d", #call, r); if (r != VK_SUCCESS) goto done; } while (0)
extern const unsigned char _binary_radv_so_start[], _binary_radv_so_end[];

#ifndef PROBE_APP
static int share_result(int fd, size_t bytes) {
    int listener = socket(AF_UNIX, SOCK_STREAM, 0), peer = -1, failed = 1;
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    strcpy(address.sun_path, SOCKET_PATH);
    /* Never unlink another run's socket. */
    if (listener < 0 || bind(listener, (struct sockaddr *)&address, sizeof(address))) {
        ps5log("STOP share bind errno=%d", errno);
        if (listener >= 0) close(listener);
        return 1;
    }
    if (listen(listener, 1)) goto done;
    ps5log("READY second-process verifier socket=%s bytes=%zu deadline=30s", SOCKET_PATH, bytes);
    struct pollfd ready = {.fd = listener, .events = POLLIN};
    if (poll(&ready, 1, 30000) != 1 || (peer = accept(listener, NULL, NULL)) < 0) goto done;
    struct iovec iov = {.iov_base = &bytes, .iov_len = sizeof(bytes)};
    union { struct cmsghdr alignment; unsigned char data[CMSG_SPACE(sizeof(int))]; } control = {0};
    struct msghdr message = {.msg_iov = &iov, .msg_iovlen = 1,
        .msg_control = control.data, .msg_controllen = sizeof(control.data)};
    struct cmsghdr *header = CMSG_FIRSTHDR(&message);
    header->cmsg_level = SOL_SOCKET; header->cmsg_type = SCM_RIGHTS;
    header->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(header), &fd, sizeof(fd));
    if (sendmsg(peer, &message, MSG_NOSIGNAL) != sizeof(bytes)) goto done;
    ready = (struct pollfd){.fd = peer, .events = POLLIN};
    char result = 0;
    if (poll(&ready, 1, 2000) == 1 && recv(peer, &result, 1, 0) == 1 && result == 'P') failed = 0;
    ps5log("second-process acknowledgement=0x%x result=%d", result, failed);
 done:
    if (peer >= 0) close(peer);
    close(listener);
    unlink(SOCKET_PATH);
    return failed;
}
#endif

int main(void) {
#ifdef PROBE_APP
    ps5log_init("ps5wl-gpu-c2b", 9530);
    pthread_t timer;
    int rc = pthread_create(&timer, NULL, deadline, NULL);
    if (rc) { ps5log("STOP timer rc=%d", rc); shell_exit(); }
    pthread_detach(timer);
    app_info_t app = {0};
    rc = sceKernelGetAppInfo(getpid(), &app);
    int foreground = sceSystemServiceGetAppIdOfRunningBigApp();
    ps5log("context pid=%d app_rc=0x%x own_app=0x%x foreground=0x%x", getpid(), rc, app.app_id, foreground);
    if (rc || !app.app_id || app.app_id != (uint32_t)foreground) shell_exit();
#else
    ps5log_init("ps5wl-gpu-c2a", 9593);
#endif
    int status = 1, fd = -1, wedged = 0;
    void *mapping = MAP_FAILED, *library = NULL;
    size_t bytes = PROBE_BYTES;
    VkInstance instance = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE, image_memory = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    const char *path = "/data/ps5wl/probe-radv.so.1";
    FILE *file = fopen(path, "wb");
    if (!file) { ps5log("STOP driver fopen errno=%d", errno); goto done; }
    size_t driver_bytes = _binary_radv_so_end - _binary_radv_so_start;
    size_t written = fwrite(_binary_radv_so_start, 1, driver_bytes, file);
    int close_result = fclose(file);
    if (written != driver_bytes || close_result) goto done;
    library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    ps5log("RADV dlopen handle=%p error=%s", library, library ? "none" : dlerror());
    if (!library) goto done;
    PFN_vkGetInstanceProcAddr get = (PFN_vkGetInstanceProcAddr)dlsym(library, "vkGetInstanceProcAddr");
    if (!get) get = (PFN_vkGetInstanceProcAddr)dlsym(library, "vk_icdGetInstanceProcAddr");
    if (!get) goto done;
    pCreateInstance = (PFN_vkCreateInstance)get(NULL, "vkCreateInstance");
    if (!pCreateInstance) goto done;
    VkApplicationInfo app_info = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "ps5wl-gpu-c2", .apiVersion = VK_API_VERSION_1_1};
    VkInstanceCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app_info};
    CHECK(pCreateInstance(&ici, NULL, &instance));
#define LOAD(name) p##name = (PFN_vk##name)get(instance, "vk" #name);
    FUNCTIONS(LOAD)
    if (!pDestroyInstance || !pEnumeratePhysicalDevices || !pGetPhysicalDeviceProperties2 ||
        !pGetPhysicalDeviceMemoryProperties || !pGetPhysicalDeviceQueueFamilyProperties ||
        !pCreateDevice || !pGetDeviceProcAddr) goto done;
    VkPhysicalDevice physical;
    uint32_t count = 1;
    CHECK(pEnumeratePhysicalDevices(instance, &count, &physical));
    if (!count) goto done;
    VkPhysicalDeviceExternalMemoryHostPropertiesEXT host = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT};
    VkPhysicalDeviceProperties2 properties = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &host};
    pGetPhysicalDeviceProperties2(physical, &properties);
    ps5log("device=%s host-pointer alignment=%llu", properties.properties.deviceName,
           (unsigned long long)host.minImportedHostPointerAlignment);
    if (!host.minImportedHostPointerAlignment || bytes % host.minImportedHostPointerAlignment) goto done;
    uint32_t families = 0;
    pGetPhysicalDeviceQueueFamilyProperties(physical, &families, NULL);
    VkQueueFamilyProperties *queues = calloc(families, sizeof(*queues));
    if (!queues) goto done;
    pGetPhysicalDeviceQueueFamilyProperties(physical, &families, queues);
    uint32_t family = UINT32_MAX;
    for (uint32_t i = 0; i < families; i++)
        if (queues[i].queueCount && (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) { family = i; break; }
    free(queues);
    if (family == UINT32_MAX) goto done;
    float priority = 1;
    VkDeviceQueueCreateInfo qci = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = family, .queueCount = 1, .pQueuePriorities = &priority};
    const char *extension = VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME;
    VkDeviceCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci,
        .enabledExtensionCount = 1, .ppEnabledExtensionNames = &extension};
    CHECK(pCreateDevice(physical, &dci, NULL, &device));
#define LOAD_DEVICE(name) do { if (!p##name) p##name = (PFN_vk##name)pGetDeviceProcAddr(device, "vk" #name); } while (0);
    FUNCTIONS(LOAD_DEVICE)
#define REQUIRE(name) if (!p##name) { ps5log("STOP missing vk%s", #name); goto done; }
    FUNCTIONS(REQUIRE)
    fd = shm_open(SHM_ANON, O_RDWR | O_CREAT, 0600);
    if (fd < 0 || ftruncate(fd, bytes)) { ps5log("STOP SHM errno=%d", errno); goto done; }
    mapping = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ps5log("SHM_ANON MAP_SHARED fd=%d bytes=%zu ptr=%p errno=%d", fd, bytes, mapping, errno);
    if (mapping == MAP_FAILED || (uintptr_t)mapping % host.minImportedHostPointerAlignment) goto done;
    memset(mapping, 0, bytes);
    VkMemoryHostPointerPropertiesEXT pointer = {.sType = VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT};
    CHECK(pGetMemoryHostPointerPropertiesEXT(device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT, mapping, &pointer));
    VkExternalMemoryBufferCreateInfo external = {.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,
        .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT};
    VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .pNext = &external,
        .size = bytes, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    CHECK(pCreateBuffer(device, &bci, NULL, &buffer));
    VkMemoryRequirements requirements;
    pGetBufferMemoryRequirements(device, buffer, &requirements);
    VkPhysicalDeviceMemoryProperties memory_properties;
    pGetPhysicalDeviceMemoryProperties(physical, &memory_properties);
    uint32_t type = UINT32_MAX;
    for (uint32_t i = 0; i < memory_properties.memoryTypeCount; i++)
        if ((pointer.memoryTypeBits & requirements.memoryTypeBits & (1u << i)) &&
            (memory_properties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) { type = i; break; }
    ps5log("import pointer-types=0x%x buffer-types=0x%x type=%u allocation-size=%llu SHM-size=%zu",
           pointer.memoryTypeBits, requirements.memoryTypeBits, type, (unsigned long long)requirements.size, bytes);
    if (type == UINT32_MAX || requirements.size > bytes) goto done;
    VkImportMemoryHostPointerInfoEXT import = {.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT, .pHostPointer = mapping};
    VkMemoryAllocateInfo mai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .pNext = &import,
        .allocationSize = bytes, .memoryTypeIndex = type};
    CHECK(pAllocateMemory(device, &mai, NULL, &memory));
    CHECK(pBindBufferMemory(device, buffer, memory, 0));
    VkCommandPoolCreateInfo pci = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = family};
    CHECK(pCreateCommandPool(device, &pci, NULL, &pool));
    VkCommandBuffer command;
    VkCommandBufferAllocateInfo cai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
    CHECK(pAllocateCommandBuffers(device, &cai, &command));
    VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    CHECK(pBeginCommandBuffer(command, &begin));
#ifdef PROBE_APP
    VkImageCreateInfo image_info = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D, .format = VK_FORMAT_R32_UINT, .extent = {64, 64, 1},
        .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL, .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    CHECK(pCreateImage(device, &image_info, NULL, &image));
    pGetImageMemoryRequirements(device, image, &requirements);
    type = UINT32_MAX;
    for (uint32_t i = 0; i < memory_properties.memoryTypeCount; i++)
        if (requirements.memoryTypeBits & (1u << i)) { type = i; break; }
    if (type == UINT32_MAX) goto done;
    mai.pNext = NULL; mai.allocationSize = requirements.size; mai.memoryTypeIndex = type;
    CHECK(pAllocateMemory(device, &mai, NULL, &image_memory));
    CHECK(pBindImageMemory(device, image, image_memory, 0));
    VkImageSubresourceRange subresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1};
    VkImageMemoryBarrier transition = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = image, .subresourceRange = subresource};
    pCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &transition);
    VkClearColorValue color = {.uint32 = {PATTERN, 0, 0, 0}};
    pCmdClearColorImage(command, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &subresource);
    transition.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    transition.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    transition.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    transition.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    pCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &transition);
    VkBufferImageCopy copy = {.imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1}, .imageExtent = {64, 64, 1}};
    pCmdCopyImageToBuffer(command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &copy);
#else
    pCmdFillBuffer(command, buffer, 0, bytes, PATTERN);
#endif
    VkMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
    pCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, NULL, 0, NULL);
    CHECK(pEndCommandBuffer(command));
    VkFenceCreateInfo fci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    CHECK(pCreateFence(device, &fci, NULL, &fence));
    VkQueue queue;
    pGetDeviceQueue(device, family, 0, &queue);
    VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &command};
    wedged = 1;
    CHECK(pQueueSubmit(queue, 1, &submit, fence));
    VkResult fence_result = pWaitForFences(device, 1, &fence, VK_TRUE, 2000000000ull);
    ps5log("vkWaitForFences timeout_ns=2000000000 result=%d", fence_result);
    if (fence_result != VK_SUCCESS) goto done;
    wedged = 0;
    status = verify(mapping, bytes);
#ifndef PROBE_APP
    if (!status) status = share_result(fd, bytes);
#endif
 done:
    if (wedged) {
        ps5log("STOP GPU wedged; no vkDeviceWaitIdle, destroy, free or unmap");
    } else {
        if (device) {
            if (fence && pDestroyFence) pDestroyFence(device, fence, NULL);
            if (pool && pDestroyCommandPool) pDestroyCommandPool(device, pool, NULL);
            if (image && pDestroyImage) pDestroyImage(device, image, NULL);
            if (buffer && pDestroyBuffer) pDestroyBuffer(device, buffer, NULL);
            if (image_memory && pFreeMemory) pFreeMemory(device, image_memory, NULL);
            if (memory && pFreeMemory) pFreeMemory(device, memory, NULL);
            if (pDestroyDevice) pDestroyDevice(device, NULL);
        }
        if (instance && pDestroyInstance) pDestroyInstance(instance, NULL);
        if (mapping != MAP_FAILED) munmap(mapping, bytes);
        if (fd >= 0) close(fd);
        if (library) dlclose(library);
        unlink(path);
    }
    ps5log("C2 RESULT %s code=%d wedged=%d", status ? "STOP" : "PASS", status, wedged);
    ps5log_flush_wait(5000);
#ifdef PROBE_APP
    shell_exit();
#else
    /* A hung GPU may still reference these pages. Keep the process alive. */
    if (wedged) for (;;) sleep(1);
#endif
    return status;
}
#endif
