#define _GNU_SOURCE
#include "wire.h"
#include "ps5wl_gpud.h"
#include <assert.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define PAGE_BYTES 16384u
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s failed\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
/* Small typed encoder for the pinned Venus 1.2.0 protocol. Scalar fields are
 * packed at 4-byte boundaries; pointers/array lengths/handles are uint64_t.
 * Field order follows vn_protocol_{driver,renderer}_{instance,device,
 * device_memory,transport}.h; no native C Vulkan struct is put on the wire. */
struct encoder { unsigned char data[4096]; size_t size; };
static void u32(struct encoder *e, uint32_t v)
{
    CHECK(e->size + 4 <= sizeof(e->data));
    memcpy(e->data + e->size, &v, 4); e->size += 4;
}
static void u64(struct encoder *e, uint64_t v) { u32(e, v); u32(e, v >> 32); }
static void command(struct encoder *e, uint32_t op, bool reply) { u32(e, op); u32(e, reply); }
static uint32_t load32(const void *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint64_t load64(const void *p) { uint64_t v; memcpy(&v, p, 8); return v; }
static void send_plain(struct gpud_wire *w, const void *p, size_t size)
{ CHECK(gpud_wire_send(w, p, size, NULL, 0)); }
static void submit(struct gpud_wire *w, struct encoder *e)
{
    struct render_context_op_submit_cmd_request req = {
        .header.op = RENDER_CONTEXT_OP_SUBMIT_CMD, .size = e->size,
    };
    size_t inline_size = e->size < sizeof(req.cmd) ? e->size : sizeof(req.cmd);
    memcpy(req.cmd, e->data, inline_size);
    send_plain(w, &req, sizeof(req));
    if (e->size > sizeof(req.cmd)) send_plain(w, e->data + sizeof(req.cmd), e->size - sizeof(req.cmd));
    e->size = 0;
}
static void fence(struct gpud_wire *w, atomic_uint *timeline, unsigned ring, uint32_t seq)
{
    struct render_context_op_submit_fence_request req = {
        .header.op = RENDER_CONTEXT_OP_SUBMIT_FENCE, .ring_index = ring, .seqno = seq,
    };
    send_plain(w, &req, sizeof(req));
    struct timespec start, now, delay = { .tv_nsec = 1000000 };
    clock_gettime(CLOCK_MONOTONIC, &start);
    while (atomic_load_explicit(timeline + ring, memory_order_acquire) != seq) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        CHECK(now.tv_sec - start.tv_sec < 5);
        nanosleep(&delay, NULL);
    }
}
static int create_blob(struct gpud_wire *w, uint32_t id, uint64_t blob, uint64_t bytes)
{
    struct render_context_op_create_resource_request req = {
        .header.op = RENDER_CONTEXT_OP_CREATE_RESOURCE, .res_id = id,
        .blob_id = blob, .blob_size = bytes, .blob_flags = 1,
    };
    send_plain(w, &req, sizeof(req));
    struct render_context_op_create_resource_reply reply;
    int fds[GPUD_RENDER_MAX_FDS]; unsigned count;
    CHECK(gpud_wire_recv(w, &reply, sizeof(reply), fds, &count) == sizeof(reply));
    CHECK(reply.fd_type == VIRGL_RESOURCE_FD_SHM && count == 1);
    struct stat st;
    CHECK(fstat(fds[0], &st) == 0 && (uint64_t)st.st_size >= bytes);
    CHECK((st.st_size % PAGE_BYTES) == 0);
    return fds[0];
}
static void seek_reply(struct encoder *e) { command(e, 179, false); u64(e, 0); }
static void result_ok(unsigned char *reply, uint32_t op)
{ CHECK(load32(reply) == op && load32(reply + 4) == 0); }

int main(int argc, char **argv)
{
    CHECK(argc == 2 && (!strcmp(argv[1], "stream") || !strcmp(argv[1], "seqpacket")));
    CHECK(setenv("PS5WL_RENDER_TRANSPORT", argv[1], 1) == 0);
    char dir[] = "/tmp/ps5wl-render-XXXXXX";
    CHECK(mkdtemp(dir));
    char render_path[108], vtest_path[108];
    snprintf(render_path, sizeof(render_path), "%s/render", dir);
    snprintf(vtest_path, sizeof(vtest_path), "%s/vtest", dir);
    struct ps5wl_gpud_config config = { .render_path = render_path, .vtest_path = vtest_path,
        .host_bytes_per_ctx = 64 * 1024 * 1024, .device_bytes_per_ctx = 64 * 1024 * 1024, .max_ctx = 8 };
    CHECK(ps5wl_gpud_start(&config) == 0);
    struct gpud_wire client = { .fd = socket(AF_UNIX, !strcmp(argv[1], "stream") ? SOCK_STREAM : SOCK_SEQPACKET, 0),
                                .stream = !strcmp(argv[1], "stream") };
    CHECK(client.fd >= 0);
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strcpy(addr.sun_path, render_path);
    CHECK(connect(client.fd, (struct sockaddr *)&addr, sizeof(addr)) == 0);
    struct render_client_op_init_request init = { .header.op = RENDER_CLIENT_OP_INIT };
    send_plain(&client, &init, sizeof(init));
    struct gpud_render_capset_request caps = { .op = GPUD_RENDER_GET_CAPSET, .capset_id = 4 };
    send_plain(&client, &caps, sizeof(caps));
    unsigned char cap_data[4096]; int fds[GPUD_RENDER_MAX_FDS]; unsigned count;
    ssize_t cap_size = gpud_wire_recv(&client, cap_data, sizeof(cap_data), fds, &count);
    CHECK(cap_size > (ssize_t)sizeof(struct gpud_render_capset_reply) && !count);
    struct gpud_render_capset_reply cap_reply;
    memcpy(&cap_reply, cap_data, sizeof(cap_reply));
    CHECK(cap_reply.capset_id == 4 && cap_reply.version == 0 && cap_reply.size == cap_size - sizeof(cap_reply));
    struct render_client_op_create_context_request create = {
        .header.op = RENDER_CLIENT_OP_CREATE_CONTEXT, .ctx_id = 17, .ctx_name = "render-host-test",
    };
    send_plain(&client, &create, sizeof(create));
    struct render_client_op_create_context_reply created;
    CHECK(gpud_wire_recv(&client, &created, sizeof(created), fds, &count) == sizeof(created));
    CHECK(created.ok && count == 1);
    struct gpud_wire ctx = { .fd = fds[0], .stream = client.stream };
    int timeline_fd = memfd_create("render-timelines", 0);
    CHECK(timeline_fd >= 0 && ftruncate(timeline_fd, PAGE_BYTES) == 0);
    atomic_uint *timeline = mmap(NULL, PAGE_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, timeline_fd, 0);
    CHECK(timeline != MAP_FAILED);
    memset(timeline, 0, PAGE_BYTES);
    struct render_context_op_init_request ci = {
        .header.op = RENDER_CONTEXT_OP_INIT, .flags = 4, .shmem_size = PAGE_BYTES,
    };
    CHECK(gpud_wire_send(&ctx, &ci, sizeof(ci), &timeline_fd, 1)); /* no eventfd */
    close(timeline_fd);
    int reply_fd = create_blob(&ctx, 1, 0, PAGE_BYTES);
    unsigned char *reply = mmap(NULL, PAGE_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, reply_fd, 0);
    CHECK(reply != MAP_FAILED); close(reply_fd);
    struct encoder e = {0};
    command(&e, 178, false); u64(&e, 1); u32(&e, 1); u64(&e, 0); u64(&e, PAGE_BYTES);
    /* vkCreateInstance: instance id 1, API 1.1, no extensions/layers. */
    command(&e, 0, true); u64(&e, 1); u32(&e, 1); u64(&e, 0); u32(&e, 0);
    u64(&e, 1); u32(&e, 0); u64(&e, 0); u64(&e, 0); u32(&e, 0);
    u64(&e, 0); u32(&e, 0); u32(&e, (1u << 22) | (1u << 12));
    u32(&e, 0); u64(&e, 0); u32(&e, 0); u64(&e, 0);
    u64(&e, 0); u64(&e, 1); u64(&e, 1);
    submit(&ctx, &e); fence(&ctx, timeline, 0, 1); result_ok(reply, 0);
    /* Enumerate one physical device and assign object id 2. */
    seek_reply(&e); command(&e, 2, true); u64(&e, 1); u64(&e, 1); u32(&e, 1); u64(&e, 1); u64(&e, 2);
    submit(&ctx, &e); fence(&ctx, timeline, 0, 2); result_ok(reply, 2);
    CHECK(load32(reply + 16) == 1 && load64(reply + 28) == 2);
    seek_reply(&e); command(&e, 8, true); u64(&e, 2); u64(&e, 1); u64(&e, 32); u64(&e, 16);
    submit(&ctx, &e); fence(&ctx, timeline, 0, 3);
    CHECK(load32(reply) == 8 && load32(reply + 12) > 0 && load32(reply + 12) <= 32);
    uint32_t type = UINT32_MAX;
    for (uint32_t i = 0; i < load32(reply + 12); i++)
        if ((load32(reply + 24 + i * 8) & 6u) == 6u) { type = i; break; }
    CHECK(type != UINT32_MAX);
    /* vkCreateDevice: one queue in family zero, no extensions/features. */
    seek_reply(&e); command(&e, 11, true); u64(&e, 2); u64(&e, 1);
    u32(&e, 3); u64(&e, 0); u32(&e, 0); u32(&e, 1); u64(&e, 1);
    u32(&e, 2); u64(&e, 0); u32(&e, 0); u32(&e, 0); u32(&e, 1); u64(&e, 1); u32(&e, 0x3f800000);
    u32(&e, 0); u64(&e, 0); u32(&e, 0); u64(&e, 0); u64(&e, 0);
    u64(&e, 0); u64(&e, 1); u64(&e, 3);
    submit(&ctx, &e); fence(&ctx, timeline, 0, 4); result_ok(reply, 11);
    seek_reply(&e); command(&e, 21, true); u64(&e, 3); u64(&e, 1);
    u32(&e, 5); u64(&e, 0); u64(&e, PAGE_BYTES); u32(&e, type);
    u64(&e, 0); u64(&e, 1); u64(&e, 4);
    submit(&ctx, &e); fence(&ctx, timeline, 0, 5); result_ok(reply, 21);
    int blob_fd = create_blob(&ctx, 2, 4, PAGE_BYTES);
    unsigned char *memory = mmap(NULL, PAGE_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, blob_fd, 0);
    CHECK(memory != MAP_FAILED); close(blob_fd);
    for (size_t i = 0; i < PAGE_BYTES; i++) memory[i] = (unsigned char)i;
    fence(&ctx, timeline, 0, 6);
    for (size_t i = 0; i < PAGE_BYTES; i++) CHECK(memory[i] == (unsigned char)i);
    /* Above validates shared host-pointer blob export, not GPU execution. */
    struct render_context_op_destroy_resource_request unref = { .header.op = RENDER_CONTEXT_OP_DESTROY_RESOURCE, .res_id = 2 };
    send_plain(&ctx, &unref, sizeof(unref));
    unref.res_id = 1; send_plain(&ctx, &unref, sizeof(unref));
    close(ctx.fd);
    struct render_client_op_destroy_context_request destroy = { .header.op = RENDER_CLIENT_OP_DESTROY_CONTEXT, .ctx_id = 17 };
    send_plain(&client, &destroy, sizeof(destroy));
    close(client.fd);
    CHECK(ps5wl_gpud_stop(5000) == 0);
    munmap(memory, PAGE_BYTES); munmap(reply, PAGE_BYTES); munmap(timeline, PAGE_BYTES);
    CHECK(rmdir(dir) == 0);
    printf("render %s: Venus instance/device, host-pointer SHM blob, timeline seqno=6 PASS\n", argv[1]);
    return 0;
}
