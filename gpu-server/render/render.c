#include "render.h"
#include "wire.h"
#include "gpud_core.h"
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#define VENUS_CAPSET 4u
#define TIMELINE_BYTES 16384u
#define MAX_CONTEXTS_PER_CLIENT 64u
struct context {
    struct context *next;
    pthread_t thread;
    struct gpud_wire wire;
    uint32_t guest_id, core_id;
    char name[33];
    atomic_uint *timelines;
    size_t timeline_size;
};
struct client {
    struct client *next;
    pthread_t thread;
    struct gpud_wire wire;
    struct context *contexts;
    atomic_bool done;
    bool initialized;
};
static struct {
    pthread_mutex_t lock;
    pthread_t accept_thread;
    struct client *clients;
    int listener;
    bool running, stream;
    char path[sizeof(((struct sockaddr_un *)0)->sun_path)];
} server = { .lock = PTHREAD_MUTEX_INITIALIZER, .listener = -1 };

static void retire(void *opaque, uint32_t ring, uint64_t fence)
{
    struct context *ctx = opaque;
    if (ring < ctx->timeline_size / sizeof(*ctx->timelines))
        atomic_store_explicit(&ctx->timelines[ring], (uint32_t)fence, memory_order_release);
}

static bool context_init(struct context *ctx, const struct render_context_op_init_request *req,
                         int fd)
{
    struct stat st;
    if (ctx->core_id || req->flags != VENUS_CAPSET ||
        req->shmem_size != TIMELINE_BYTES || fstat(fd, &st) || st.st_size < TIMELINE_BYTES)
        return false;
    ctx->timelines = mmap(NULL, req->shmem_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ctx->timelines == MAP_FAILED) { ctx->timelines = NULL; return false; }
    ctx->timeline_size = req->shmem_size;
    for (size_t i = 0; i < req->shmem_size / sizeof(*ctx->timelines); i++)
        atomic_init(&ctx->timelines[i], 0);
    ctx->core_id = gpud_core_context_create(ctx->name, retire, ctx);
    return ctx->core_id != 0;
}

static bool context_dispatch(struct context *ctx)
{
    union render_context_op_request req;
    int fds[GPUD_RENDER_MAX_FDS];
    unsigned count;
    ssize_t n = gpud_wire_recv(&ctx->wire, &req, sizeof(req), fds, &count);
    if (n < (ssize_t)sizeof(req.header)) {
        for (unsigned i = 0; i < count; i++) close(fds[i]);
        return false;
    }
    bool ok = false;
    if (req.header.op == RENDER_CONTEXT_OP_INIT) {
        ok = n == sizeof(req.init) && count == 1 && context_init(ctx, &req.init, fds[0]);
    } else if (!count && ctx->core_id) {
        switch (req.header.op) {
        case RENDER_CONTEXT_OP_NOP:
            ok = n == sizeof(req.nop);
            break;
        case RENDER_CONTEXT_OP_CREATE_RESOURCE: {
            if (n != sizeof(req.create_resource) || !req.create_resource.res_id ||
                !req.create_resource.blob_size || req.create_resource.blob_size > UINT64_MAX - 16383 ||
                (req.create_resource.blob_flags & ~1u)) break;
            struct render_context_op_create_resource_reply reply = { .fd_type = VIRGL_RESOURCE_FD_INVALID };
            uint64_t size = (req.create_resource.blob_size + 16383) & ~UINT64_C(16383);
            int fd = gpud_core_blob(ctx->core_id, req.create_resource.res_id,
                                   req.create_resource.blob_id, size,
                                   req.create_resource.blob_flags, &reply.map_info);
            if (fd >= 0) reply.fd_type = VIRGL_RESOURCE_FD_SHM;
            ok = gpud_wire_send(&ctx->wire, &reply, sizeof(reply), fd >= 0 ? &fd : NULL, fd >= 0);
            if (fd >= 0) close(fd);
            break;
        }
        case RENDER_CONTEXT_OP_DESTROY_RESOURCE:
            if (n != sizeof(req.destroy_resource)) break;
            gpud_core_unref(ctx->core_id, req.destroy_resource.res_id);
            ok = true;
            break;
        case RENDER_CONTEXT_OP_SUBMIT_FENCE:
            if (n != sizeof(req.submit_fence) || (req.submit_fence.flags & ~1u) ||
                req.submit_fence.ring_index >= ctx->timeline_size / sizeof(atomic_uint)) break;
            ok = gpud_core_fence(ctx->core_id, req.submit_fence.flags,
                                 req.submit_fence.ring_index, req.submit_fence.seqno);
            break;
        case RENDER_CONTEXT_OP_SUBMIT_CMD: {
            if (n != sizeof(req.submit_cmd) || !req.submit_cmd.size ||
                req.submit_cmd.size % 4 || req.submit_cmd.size > GPUD_RENDER_MAX_FRAME) break;
            void *cmd = req.submit_cmd.cmd;
            if (req.submit_cmd.size > sizeof(req.submit_cmd.cmd)) {
                cmd = malloc(req.submit_cmd.size);
                if (!cmd) break;
                memcpy(cmd, req.submit_cmd.cmd, sizeof(req.submit_cmd.cmd));
                unsigned extra_count;
                ssize_t extra = gpud_wire_recv(&ctx->wire, (char *)cmd + sizeof(req.submit_cmd.cmd),
                                               req.submit_cmd.size - sizeof(req.submit_cmd.cmd), fds, &extra_count);
                for (unsigned i = 0; i < extra_count; i++) close(fds[i]);
                if (extra != (ssize_t)(req.submit_cmd.size - sizeof(req.submit_cmd.cmd)) || extra_count) {
                    free(cmd);
                    break;
                }
            }
            ok = gpud_core_submit(ctx->core_id, cmd, req.submit_cmd.size);
            if (cmd != req.submit_cmd.cmd) free(cmd);
            break;
        }
        default: break; /* imports and unknown operations have no supported semantics */
        }
    }
    for (unsigned i = 0; i < count; i++) close(fds[i]);
    return ok;
}

static void *context_run(void *arg)
{
    struct context *ctx = arg;
    while (context_dispatch(ctx)) {}
    if (ctx->core_id) gpud_core_context_destroy(ctx->core_id);
    if (ctx->timelines) munmap(ctx->timelines, ctx->timeline_size);
    shutdown(ctx->wire.fd, SHUT_RDWR);
    return NULL;
}

static void destroy_context(struct context **link)
{
    struct context *ctx = *link;
    *link = ctx->next;
    shutdown(ctx->wire.fd, SHUT_RDWR);
    pthread_join(ctx->thread, NULL);
    close(ctx->wire.fd);
    free(ctx);
}

static bool create_context(struct client *client, const struct render_client_op_create_context_request *req)
{
    struct render_client_op_create_context_reply reply = { .ok = false };
    unsigned count = 0;
    for (struct context *c = client->contexts; c; c = c->next) {
        if (c->guest_id == req->ctx_id) return gpud_wire_send(&client->wire, &reply, sizeof(reply), NULL, 0);
        count++;
    }
    struct context *ctx = count < MAX_CONTEXTS_PER_CLIENT && req->ctx_id ? calloc(1, sizeof(*ctx)) : NULL;
    int pair[2];
    if (!ctx) return gpud_wire_send(&client->wire, &reply, sizeof(reply), NULL, 0);
    if (socketpair(AF_UNIX, client->wire.stream ? SOCK_STREAM : SOCK_SEQPACKET, 0, pair)) {
        free(ctx);
        return gpud_wire_send(&client->wire, &reply, sizeof(reply), NULL, 0);
    }
    if (gpud_wire_cloexec(pair[0]) || gpud_wire_cloexec(pair[1])) goto fail;
    ctx->wire = (struct gpud_wire){ pair[0], client->wire.stream };
    ctx->guest_id = req->ctx_id;
    memcpy(ctx->name, req->ctx_name, sizeof(req->ctx_name));
    if (pthread_create(&ctx->thread, NULL, context_run, ctx)) goto fail;
    ctx->next = client->contexts;
    client->contexts = ctx;
    reply.ok = true;
    bool ok = gpud_wire_send(&client->wire, &reply, sizeof(reply), &pair[1], 1);
    close(pair[1]);
    return ok;
fail:
    close(pair[0]);
    close(pair[1]);
    free(ctx);
    return gpud_wire_send(&client->wire, &reply, sizeof(reply), NULL, 0);
}

static bool client_dispatch(struct client *client)
{
    union { union render_client_op_request upstream; struct gpud_render_capset_request capset; } req;
    int fds[GPUD_RENDER_MAX_FDS];
    unsigned count;
    ssize_t n = gpud_wire_recv(&client->wire, &req, sizeof(req), fds, &count);
    for (unsigned i = 0; i < count; i++) close(fds[i]);
    if (n < (ssize_t)sizeof(req.upstream.header) || count) return false;
    if (req.upstream.header.op == RENDER_CLIENT_OP_INIT) {
        if (n != sizeof(req.upstream.init) || client->initialized) return false;
        client->initialized = true; /* vkr was already initialized in this process */
        return true;
    }
    if (!client->initialized) return false;
    switch ((unsigned)req.upstream.header.op) {
    case RENDER_CLIENT_OP_NOP: return n == sizeof(req.upstream.nop);
    case RENDER_CLIENT_OP_RESET:
        if (n != sizeof(req.upstream.reset)) return false;
        while (client->contexts) destroy_context(&client->contexts);
        return true;
    case RENDER_CLIENT_OP_CREATE_CONTEXT:
        return n == sizeof(req.upstream.create_context) && create_context(client, &req.upstream.create_context);
    case RENDER_CLIENT_OP_DESTROY_CONTEXT: {
        if (n != sizeof(req.upstream.destroy_context)) return false;
        struct context **link = &client->contexts;
        while (*link && (*link)->guest_id != req.upstream.destroy_context.ctx_id) link = &(*link)->next;
        if (*link) destroy_context(link);
        return true;
    }
    case GPUD_RENDER_GET_CAPSET: {
        if (n != sizeof(req.capset) || req.capset.capset_id != VENUS_CAPSET || req.capset.version) return false;
        size_t bytes = gpud_core_capset(NULL);
        if (bytes > GPUD_RENDER_MAX_FRAME - sizeof(struct gpud_render_capset_reply)) return false;
        struct gpud_render_capset_reply *reply = calloc(1, sizeof(*reply) + bytes);
        if (!reply) return false;
        *reply = (struct gpud_render_capset_reply){ VENUS_CAPSET, 0, bytes };
        gpud_core_capset(reply + 1);
        bool ok = gpud_wire_send(&client->wire, reply, sizeof(*reply) + bytes, NULL, 0);
        free(reply);
        return ok;
    }
    default: return false;
    }
}

static void *client_run(void *arg)
{
    struct client *client = arg;
    while (client_dispatch(client)) {}
    while (client->contexts) destroy_context(&client->contexts);
    shutdown(client->wire.fd, SHUT_RDWR);
    atomic_store(&client->done, true);
    return NULL;
}

static void reap_clients(bool all)
{
    struct client **link = &server.clients;
    while (*link) {
        struct client *client = *link;
        if (!all && !atomic_load(&client->done)) { link = &client->next; continue; }
        *link = client->next;
        pthread_join(client->thread, NULL);
        close(client->wire.fd);
        free(client);
    }
}

static void *accept_run(void *unused)
{
    (void)unused;
    for (;;) {
        int fd = accept(server.listener, NULL, NULL);
        if (fd < 0) { if (errno == EINTR) continue; break; }
        struct client *client = calloc(1, sizeof(*client));
        if (!client || gpud_wire_cloexec(fd)) { free(client); close(fd); continue; }
        client->wire = (struct gpud_wire){ fd, server.stream };
        atomic_init(&client->done, false);
        pthread_mutex_lock(&server.lock);
        reap_clients(false);
        if (!server.running || pthread_create(&client->thread, NULL, client_run, client)) {
            pthread_mutex_unlock(&server.lock);
            close(fd);
            free(client);
            continue;
        }
        client->next = server.clients;
        server.clients = client;
        pthread_mutex_unlock(&server.lock);
    }
    return NULL;
}

int gpud_render_start(const char *path)
{
    if (!path || !*path || strlen(path) >= sizeof(server.path)) return -EINVAL;
    const char *transport = getenv("PS5WL_RENDER_TRANSPORT");
    if (transport && strcmp(transport, "stream") && strcmp(transport, "seqpacket")) return -EINVAL;
    if (server.listener >= 0) return -EALREADY;
    server.stream = !transport || !strcmp(transport, "stream");
    int fd = socket(AF_UNIX, server.stream ? SOCK_STREAM : SOCK_SEQPACKET, 0);
    if (fd < 0) return -errno;
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strcpy(addr.sun_path, path);
    if (gpud_wire_cloexec(fd) || bind(fd, (struct sockaddr *)&addr, sizeof(addr))) {
        int error = errno; close(fd); return -error;
    }
    if (listen(fd, 16)) { int error = errno; close(fd); unlink(path); return -error; }
    server.listener = fd;
    strcpy(server.path, path);
    server.running = true;
    int error = pthread_create(&server.accept_thread, NULL, accept_run, NULL);
    if (error) { close(fd); unlink(path); server.listener = -1; server.running = false; return -error; }
    return 0;
}

void gpud_render_stop(void)
{
    if (server.listener < 0) return;
    pthread_mutex_lock(&server.lock);
    server.running = false;
    shutdown(server.listener, SHUT_RDWR);
    for (struct client *c = server.clients; c; c = c->next) shutdown(c->wire.fd, SHUT_RDWR);
    pthread_mutex_unlock(&server.lock);
    pthread_join(server.accept_thread, NULL);
    close(server.listener);
    server.listener = -1;
    reap_clients(true);
    unlink(server.path);
}
