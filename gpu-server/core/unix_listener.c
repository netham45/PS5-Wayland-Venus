#include "unix_listener.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#ifdef PS5WL_PS5
#include "ps5log.h"
#endif

int gpud_unix_bind(int fd, const char *path, int type)
{
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    if (strlen(path) >= sizeof(addr.sun_path)) { errno = ENAMETOOLONG; return -1; }
    strcpy(addr.sun_path, path);
    if (!bind(fd, (struct sockaddr *)&addr, sizeof(addr))) return 0;
    if (errno != EADDRINUSE) return -1;

    struct stat before, after;
    if (lstat(path, &before) || !S_ISSOCK(before.st_mode)) {
        errno = EADDRINUSE; return -1;
    }
    int probe = socket(AF_UNIX, type, 0);
    if (probe < 0) return -1;
    int flags = fcntl(probe, F_GETFL);
    if (flags < 0 || fcntl(probe, F_SETFL, flags | O_NONBLOCK) < 0) {
        int error = errno; close(probe); errno = error; return -1;
    }
    int result = connect(probe, (struct sockaddr *)&addr, sizeof(addr));
    int error = errno;
    close(probe);
    /* A full live backlog, incompatible transport, or any other failure is
     * not proof of a dead endpoint. Recheck identity before removing it. */
    if (!result || error != ECONNREFUSED || lstat(path, &after) ||
        before.st_dev != after.st_dev || before.st_ino != after.st_ino) {
        errno = EADDRINUSE; return -1;
    }
    if (unlink(path)) return -1;
#ifdef PS5WL_PS5
    ps5log("gpud: removed refused stale socket path=%s", path);
#else
    fprintf(stderr, "gpud: removed refused stale socket path=%s\n", path);
#endif
    return bind(fd, (struct sockaddr *)&addr, sizeof(addr));
}
