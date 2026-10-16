/* WAIT_UINT/WAKE control-page protocol; PRIVATE is a negative contrast. */
#include "cap.h"
#include <sys/umtx.h>

int main(void) {
    begin("lklcap-umtx", 9604);
    int fd = shm_open(SHM_ANON, O_RDWR, 0600);
    if (fd < 0 || ftruncate(fd, PAGE)) {
        CHECK("shared_setup", 0, "errno=%d", errno);
        if (fd >= 0) close(fd);
        return finish();
    }
    unsigned *p = mmap(NULL, PAGE, 3, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) { close(fd); CHECK("shared_map", 0, "errno=%d", errno); return finish(); }
    for (int private = 0; private < 2; private++) {
        p[0] = p[1] = 0;
        pid_t pid = 0;
        int s = helper(fd, private ? 3 : 2, &pid), result[4] = {-999, 0, 0, 0};
        long r = -1;
        unsigned attempts = 0;
        if (s >= 0) {
            double end = now() + 3;
            while (!__atomic_load_n(p + 1, __ATOMIC_ACQUIRE) && now() < end) usleep(1000);
            /* No word change: success must be a wake, not the WAIT mismatch fast path.
             * Repeat bounded wakes to avoid a false negative if the waiter was descheduled. */
            end = now() + 3;
            while (!ready(s, 0) && now() < end) {
                r = RAW(SYS__umtx_op,p,private ? UMTX_OP_WAKE_PRIVATE : UMTX_OP_WAKE,1,0,0);
                attempts++;
                usleep(10000);
            }
            if (ready(s, 100)) full(s, result, sizeof(result), 0);
            close(s);
        }
        ps5log("VALUE wake private=%d rc=%ld errno=%ld helper=%d attempts=%u",
            private, r, r < 0 ? -r : 0, pid, attempts);
        CHECK(private ? "PRIVATE_cross_process_contrast" : "shared_cross_process_wait_wake",
            private ? result[0] == -ETIMEDOUT : result[0] == 0,
            "helper=%d result=%d errno=%d word=%d expected=%s",
            pid, result[0], result[1], result[2], private ? "timeout" : "wake");
        /* Helper's logger lingers 2.5 s; do not overlap helper log listeners. */
        if (!private) usleep(2700000);
    }
    munmap(p, PAGE);
    close(fd);
    return finish();
}
