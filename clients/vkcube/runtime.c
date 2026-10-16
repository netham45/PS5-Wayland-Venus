#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "ps5log.h"

#ifdef PS5WL_PS5
extern const unsigned char _binary_radv_so_start[], _binary_radv_so_end[];
#endif

int ps5wl_vulkan_install(void) {
#ifdef PS5WL_PS5
    const char *directory = "/system_tmp/ps5wl";
    const char *path = "/system_tmp/ps5wl/libvulkan.so.1";
    if (mkdir(directory, 0700) && errno != EEXIST) return -1;
    FILE *file = fopen(path, "wb");
    if (!file) return -1;
    size_t length = (size_t)(_binary_radv_so_end - _binary_radv_so_start);
    size_t written = fwrite(_binary_radv_so_start, 1, length, file);
    int rc = fclose(file);
    if (written != length || rc) return -1;
    ps5log("RADV library extracted bytes=%zu path=%s", length, path);
    return 0;
#else
    return 0;
#endif
}
