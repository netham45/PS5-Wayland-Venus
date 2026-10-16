#include "cap.h"

int main(void) {
    begin("lklcap-network", 9609);
    uid_t original = geteuid();
    int dropped = seteuid(getuid());
    CHECK("unprivileged_credentials", dropped == 0 && geteuid() != 0,
        "seteuid_rc=%d errno=%d real=%d effective=%d original_effective=%d",
        dropped, dropped ? errno : 0, getuid(), geteuid(), original);
    if (failures) return finish();
    for (int type = SOCK_STREAM; type <= SOCK_DGRAM; type++) {
        int l = socket(AF_INET, type, 0), c = -1;
        struct sockaddr_in a = {.sin_len = sizeof(a), .sin_family = AF_INET,
            .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
        int rc = l < 0 ? -1 : bind(l, (void*)&a, sizeof(a));
        socklen_t len = sizeof(a);
        if (!rc) rc = getsockname(l, (void*)&a, &len);
        if (!rc && type == SOCK_STREAM) rc = listen(l, 1);
        CHECK(type == SOCK_STREAM ? "TCP_listen" : "UDP_bind", rc == 0,
            "fd=%d rc=%d errno=%d port=%u", l, rc, rc ? errno : 0, ntohs(a.sin_port));
        if (!rc) {
            c = socket(AF_INET, type, 0);
            rc = connect(c, (void*)&a, sizeof(a));
            CHECK(type == SOCK_STREAM ? "TCP_client" : "UDP_client", rc == 0,
                "fd=%d rc=%d errno=%d", c, rc, rc ? errno : 0);
            int accepted = type == SOCK_STREAM ? (ready(l, 1000) ? accept(l, NULL, NULL) : -1) : l;
            char b = 'L', v = 0;
            if (!rc && accepted >= 0) {
                write(c, &b, 1);
                rc = ready(accepted, 1000) ? read(accepted, &v, 1) : -1;
                CHECK("loopback_data", rc == 1 && v == 'L',
                    "type=%d rc=%d byte=%d errno=%d", type, rc, v, rc < 0 ? errno : 0);
            } else CHECK("loopback_endpoint", 0, "type=%d fd=%d errno=%d", type, accepted, errno);
            if (type == SOCK_STREAM && accepted >= 0) close(accepted);
        }
        if (c >= 0) close(c);
        if (l >= 0) close(l);
    }
    int types[] = {SOCK_RAW, SOCK_DGRAM};
    for (int i = 0; i < 2; i++) {
        int s = socket(AF_INET, types[i], IPPROTO_ICMP), err = errno;
        CHECK(i ? "ICMP_datagram" : "ICMP_raw", s >= 0,
            "fd=%d errno=%d uid=%d euid=%d", s, s < 0 ? err : 0, getuid(), geteuid());
        if (s >= 0) close(s);
    }
    /* Process exit ends the intentionally unprivileged credential scope. */
    return finish();
}
