#include "cap.h"
#include <sys/sysctl.h>
#include <sys/user.h>
#include <stddef.h>

int main(void) {
    begin("lklcap-introspection", 9611);
    int mib[] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid()};
    struct kinfo_proc proc = {0};
    size_t size = sizeof(proc);
    int rc = sysctl(mib, 4, &proc, &size, NULL, 0);
    CHECK("KERN_PROC_PID", rc == 0,
        "rc=%d errno=%d bytes=%zu pid=%d rss_pages=%lu virtual_bytes=%lu threads=%d",
        rc, errno, size, rc ? 0 : proc.ki_pid, rc ? 0 : proc.ki_rssize,
        rc ? 0 : proc.ki_size, rc ? 0 : proc.ki_numthreads);
    mib[2] = KERN_PROC_VMMAP;
    size = 0;
    rc = sysctl(mib, 4, NULL, &size, NULL, 0);
    CHECK("KERN_PROC_VMMAP_size", rc == 0, "rc=%d errno=%d bytes=%zu", rc, errno, size);
    if (!rc && size && size < 16 * 1024 * 1024) {
        size += size / 2;
        char *buffer = malloc(size);
        if (!buffer) { CHECK("map_buffer", 0, "bytes=%zu errno=%d", size, errno); return finish(); }
        rc = sysctl(mib, 4, buffer, &size, NULL, 0);
        unsigned entries = 0;
        size_t off = 0;
        while (!rc && off + sizeof(int) <= size) {
            struct kinfo_vmentry *v = (void*)(buffer + off);
            if (v->kve_structsize < (int)offsetof(struct kinfo_vmentry, kve_path) ||
                (size_t)v->kve_structsize > size - off) break;
            ps5log("MAP start=%lx end=%lx prot=%x type=%d resident=%d",
                v->kve_start, v->kve_end, v->kve_protection, v->kve_type, v->kve_resident);
            entries++;
            off += v->kve_structsize;
        }
        CHECK("KERN_PROC_VMMAP_entries", rc == 0 && entries > 0 && off == size,
            "rc=%d errno=%d entries=%u bytes=%zu parsed=%zu", rc, errno, entries, size, off);
        free(buffer);
    }
    return finish();
}
