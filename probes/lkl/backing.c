/* Projection/backing checks from freebsd_runtime_memory and freebsd_shared_vm.
 * Every MAP_FIXED target is first reserved non-destructively by this payload. */
#include "cap.h"

int main(void) {
    begin("lklcap-backing", 9601);
    int fd = shm_open(SHM_ANON, O_RDWR, 0600);
    CHECK("SHM_ANON", fd >= 0, "fd=%d errno=%d", fd, fd<0?errno:0);
    if (fd < 0) return finish();
    uint64_t biggest = 0, biggest_object = 0;
    for (uint64_t gb = 1; gb <= 8; gb *= 2) {
        uint64_t size = gb << 30;
        int rc = ftruncate(fd, size), err = errno;
        void *p = rc ? MAP_FAILED : mmap(NULL, size, 3, MAP_SHARED, fd, 0);
        int map_errno = p == MAP_FAILED ? errno : 0;
        if (p != MAP_FAILED) {
            /* 1 MiB committed at most, including the last page. */
            for (unsigned j = 0; j < 64; j++)
                *((volatile char*)p + (size - PAGE) * j / 63 / PAGE * PAGE) = j;
            biggest = size;
            munmap(p, size);
        }
        CHECK("large_contiguous_shm_map", p != MAP_FAILED,
            "GiB=%llu truncate=%d map=%p errno=%d", (unsigned long long)gb, rc, p, rc ? err : map_errno);
        int windows = 0, window_errno = rc ? err : 0;
        if (!rc) for (unsigned j = 0; j < 64; j++) {
            off_t off = (size - PAGE) * j / 63 / PAGE * PAGE;
            void *window = mmap(NULL, PAGE, 3, MAP_SHARED, fd, off);
            if (window == MAP_FAILED) { window_errno = errno; break; }
            *(volatile unsigned*)window = j;
            munmap(window, PAGE);
            windows++;
        }
        if (windows == 64) biggest_object = size;
        CHECK("large_shm_object_windowed", windows == 64,
            "GiB=%llu windows=%d last_offset=%llu errno=%d", (unsigned long long)gb,
            windows, (unsigned long long)(size - PAGE), window_errno);
    }
    ps5log("VALUE largest_tested_contiguous_shm=%llu largest_tested_object=%llu not_absolute_limits=1",
        (unsigned long long)biggest, (unsigned long long)biggest_object);
    if (!biggest) { close(fd); return finish(); }
    const uintptr_t addresses[] = {
        0x400000, 0x10000000, 0x100000000, 0x1000000000,
        0x8000000000, 0xff00000000, 0xffffffc000,
        0x10000000000, 0x100000000000, 0x400000000000,
        0x7f0000000000, 0x7ffffff00000
    };
    for (unsigned j = 0; j < sizeof(addresses) / sizeof(*addresses); j++) {
        void *want = (void*)addresses[j];
        void *reserve = mmap(want, PAGE, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (reserve != want) {
            int err = errno;
            if (reserve != MAP_FAILED) munmap(reserve, PAGE);
            CHECK("VA_reservation", 0, "address=%p collision_or_denial=1 errno=%d", want, err);
            long raw_reserve=raw6(SYS_mmap,(long)want,PAGE,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);
            long raw_map=-1,raw_protect=-1;
            if(raw_reserve==(long)want){
                raw_map=raw6(SYS_mmap,(long)want,PAGE,3,MAP_SHARED|MAP_FIXED,fd,0);
                if(raw_map==(long)want){*(volatile unsigned*)want=42;raw_protect=RAW(SYS_mprotect,want,PAGE,PROT_READ,0,0);}
            }
            CHECK("raw_high_VA_projection",raw_map==(long)want&&raw_protect==0,
                "address=%p reserve_rc=%lx map_rc=%lx protect_rc=%ld reserve_errno=%ld",
                want,(unsigned long)raw_reserve,(unsigned long)raw_map,raw_protect,raw_reserve<0?-raw_reserve:0);
            if(raw_reserve>=0)RAW(SYS_munmap,raw_reserve,PAGE,0,0,0);
            continue;
        }
        int good = 1, err = 0, cycles = 0;
        for (int k = 0; k < 8; k++) {
            void *p = mmap(want, PAGE, 3, MAP_SHARED | MAP_FIXED, fd, (k % 2) * PAGE);
            if (p != want) { good = 0; err = errno; break; }
            *(volatile unsigned*)p = k;
            if (mprotect(p, PAGE, PROT_READ) || mprotect(p, PAGE, 3) || munmap(p, PAGE)) {
                good = 0; err = errno; break;
            }
            cycles++;
            reserve = mmap(want, PAGE, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
            if (reserve != want) {
                if (reserve != MAP_FAILED) munmap(reserve, PAGE);
                good = 0; err = errno; reserve = MAP_FAILED; break;
            }
        }
        CHECK("MAP_FIXED_cycles", good, "address=%p cycles=%d errno=%d", want, cycles, err);
        if (reserve != MAP_FAILED) munmap(want, PAGE);
    }
    void *reserve = mmap(NULL, 4 * PAGE, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (reserve != MAP_FAILED) {
        void *p = mmap((char*)reserve + 4096, 4096, 3, MAP_SHARED | MAP_FIXED, fd, 0);
        CHECK("4K_address", p != MAP_FAILED, "address=%p map=%p errno=%d", (char*)reserve + 4096, p, errno);
        p = mmap(reserve, PAGE, 3, MAP_SHARED | MAP_FIXED, fd, 4096);
        CHECK("4K_offset", p != MAP_FAILED, "offset=4096 map=%p errno=%d native_page=%d", p, errno, getpagesize());
        long native = raw6(SYS_mmap,(long)reserve,PAGE,3,MAP_SHARED|MAP_FIXED,fd,0);
        CHECK("raw_native_page_control",native==(long)reserve,
            "rc=%lx errno=%ld expected=%p",(unsigned long)native,native<0?-native:0,reserve);
        if(native==(long)reserve){
            *(volatile unsigned*)((char*)reserve+4096)=0x12345678;
            long address = raw6(SYS_mmap,(long)reserve+4096,4096,3,MAP_SHARED|MAP_FIXED,fd,0);
            CHECK("raw_4K_address",address==(long)reserve+4096,
                "rc=%lx errno=%ld expected=%p",(unsigned long)address,address<0?-address:0,(char*)reserve+4096);
            long offset = raw6(SYS_mmap,(long)reserve,PAGE,3,MAP_SHARED|MAP_FIXED,fd,4096);
            unsigned first = offset==(long)reserve?*(volatile unsigned*)reserve:0;
            CHECK("raw_4K_offset",offset==(long)reserve&&first==0x12345678,
                "rc=%lx errno=%ld first=%x expected=12345678",(unsigned long)offset,offset<0?-offset:0,first);
            long protect = RAW(SYS_mprotect,(char*)reserve+4096,4096,PROT_READ,0,0);
            CHECK("raw_4K_mprotect",protect==0,"rc=%ld errno=%ld",protect,protect<0?-protect:0);
        }
        munmap(reserve, 4 * PAGE);
    } else CHECK("granularity_reservation", 0, "errno=%d", errno);
    close(fd);
    return finish();
}
