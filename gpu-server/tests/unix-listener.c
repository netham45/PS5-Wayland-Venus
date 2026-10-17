#include "unix_listener.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void)
{
    char directory[] = "/tmp/gpud-listener-XXXXXX";
    assert(mkdtemp(directory));
    char path[128];
    snprintf(path, sizeof(path), "%s/socket", directory);
    const int types[] = { SOCK_STREAM, SOCK_SEQPACKET };
    for (unsigned i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        int owner = socket(AF_UNIX, types[i], 0);
        int next = socket(AF_UNIX, types[i], 0);
        assert(owner >= 0 && next >= 0);
        assert(!gpud_unix_bind(owner, path, types[i]));
        assert(!listen(owner, 4));
        struct stat before, after;
        assert(!lstat(path, &before));
        assert(gpud_unix_bind(next, path, types[i]) == -1 && errno == EADDRINUSE);
        assert(!lstat(path, &after) && before.st_ino == after.st_ino);
        close(owner);
        assert(!gpud_unix_bind(next, path, types[i]));
        assert(!listen(next, 4));
        close(next);
        assert(!unlink(path));
    }
    int file = open(path, O_CREAT | O_WRONLY | O_EXCL, 0600);
    assert(file >= 0);
    close(file);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    assert(fd >= 0);
    assert(gpud_unix_bind(fd, path, SOCK_STREAM) == -1 && errno == EADDRINUSE);
    struct stat st;
    assert(!lstat(path, &st) && S_ISREG(st.st_mode));
    close(fd);
    assert(!unlink(path));
    assert(!rmdir(directory));
    puts("PASS: stream/seqpacket stale recovery; live listeners and regular files retained");
    return 0;
}
