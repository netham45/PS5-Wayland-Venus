#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "ps5log.h"

#ifdef PS5WL_PS5
extern const unsigned char _binary_radv_so_start[], _binary_radv_so_end[];
#endif

int ps5wl_vulkan_install(void) {
#ifdef PS5WL_PS5
    /* pacbrew Mesa patch 0028 uses /data for payload-owned writable storage.
     * /system_tmp is too small for the driver (measured ENOSPC at 256 KiB).
     */
    const char *directory = "/data/ps5wl";
    const char *path = "/data/ps5wl/libvulkan.so.1";
    if (mkdir(directory, 0700) && errno != EEXIST) {
        ps5log("RADV mkdir path=%s errno=%d %s", directory, errno, strerror(errno));
        return -1;
    }
    /* Never truncate an inode another Vulkan client may still have mapped. */
    char temporary[128];
    snprintf(temporary, sizeof(temporary), "%s.tmp.%d", path, getpid());
    FILE *file = fopen(temporary, "wb");
    if (!file) {
        ps5log("RADV fopen path=%s errno=%d %s", temporary, errno, strerror(errno));
        return -1;
    }
    size_t length = (size_t)(_binary_radv_so_end - _binary_radv_so_start);
    size_t written = fwrite(_binary_radv_so_start, 1, length, file);
    int rc = fclose(file);
    if (written != length || rc) {
        ps5log("RADV write bytes=%zu expected=%zu close=%d errno=%d %s",
            written, length, rc, errno, strerror(errno));
        unlink(temporary);
        return -1;
    }
    if (rename(temporary, path)) {
        ps5log("RADV rename path=%s errno=%d %s", path, errno, strerror(errno));
        unlink(temporary);
        return -1;
    }
    ps5log("RADV library extracted bytes=%zu path=%s", length, path);
    return 0;
#else
    return 0;
#endif
}
