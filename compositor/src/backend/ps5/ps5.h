/* PS5 ABI declarations transcribed from ps5-payload-dev/SDL release-2.30.x-ps5
 * SDL_ps5video.h and SDL_ps5joystick.h; mouse ABI from prospero-win pw_hid_ps5.c. */
#ifndef PS5WL_PS5_H
#define PS5WL_PS5_H
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
struct kevent;
#include <wayland-server-core.h>
#include <wlr/backend.h>
struct ps5_video_buffer { void *data; uint64_t reserved[3]; };
struct ps5_video_attr { uint8_t reserved[80]; };
int sceSystemServiceHideSplashScreen(void);
/* Full-pool allocator/query ABI: Mesa PS5 ps5_sce.h:46-53. */
int64_t sceKernelGetDirectMemorySize(void);
int sceKernelAvailableDirectMemorySize(int64_t, int64_t, size_t, int64_t *, size_t *);
int sceKernelAllocateDirectMemory(int64_t, int64_t, size_t, size_t, int, int64_t *);
int sceKernelMapDirectMemory(void **, size_t, int, int, intptr_t, size_t);
int sceKernelReleaseDirectMemory(intptr_t, size_t);
int sceKernelMunmap(void *, size_t);
int sceKernelCreateEqueue(struct kevent **, const char *);
int sceKernelWaitEqueue(struct kevent *, struct kevent *, int, int *, unsigned *);
int sceKernelDeleteEqueue(struct kevent *);
int sceVideoOutOpen(int, int, int, const void *);
void sceVideoOutClose(int);
int sceVideoOutAddFlipEvent(struct kevent *, int, void *);
int sceVideoOutDeleteFlipEvent(struct kevent *, int);
int sceVideoOutSetFlipRate(int, int);
int sceVideoOutSubmitFlip(int, int, uint32_t, int64_t);
/* PS5_Vulkan driver/ps5vk_private.h:1718-1721,1998ff: 16 u64 words,
 * status[3] is the marker of the latest displayed flip (not PS4's struct). */
int sceVideoOutGetFlipStatus(int, void *);
void sceVideoOutSetBufferAttribute2(struct ps5_video_attr *, uint64_t, uint32_t,
 uint32_t, uint32_t, uint64_t, uint32_t, uint64_t);
int sceVideoOutRegisterBuffers2(int, int, int, struct ps5_video_buffer *, int,
 struct ps5_video_attr *, int, void *);
int sceUserServiceInitialize(void *);
int sceUserServiceGetForegroundUser(int *);
int sceKeyboardInit(void);
int sceKeyboardOpen(int, int, int, void *);
int sceKeyboardClose(int);
int sceKeyboardReadState(int, void *);
int sceSysmoduleLoadModule(uint16_t);
int sceSysmoduleGetModuleHandleInternal(uint32_t, int32_t *);
int sceKernelDlsym(int32_t, const char *, void **);
struct ps5_pad_touch { uint16_t x, y; uint8_t finger, pad[3]; };
struct ps5_pad_data {
 uint32_t buttons;
 uint8_t lx, ly, rx, ry, l2, r2;
 uint16_t padding;
 float quat[4], vel[3], accel[3];
 struct { uint8_t fingers, pad[3]; uint32_t reserved; struct ps5_pad_touch touch[2]; } touch;
 uint8_t connected;
 uint64_t timestamp;
 uint8_t ext[16], count, unknown[15];
};
int scePadInit(void);
int scePadOpen(int, int, int, void *);
int scePadClose(int);
int scePadReadState(int, struct ps5_pad_data *);
struct wlr_backend *ps5_backend_create(struct wl_display *display);
struct ps5_input;
struct ps5_input *ps5_input_create(struct wlr_backend *, struct wl_display *);
void ps5_input_start(struct ps5_input *);
void ps5_input_destroy(struct ps5_input *);
struct ps5_capture;
struct ps5_capture *ps5_capture_create(struct wl_event_loop *, unsigned, unsigned,
 uint32_t (*)(void *, unsigned, unsigned), void *);
void ps5_capture_destroy(struct ps5_capture *);
void ps5_autostart(void);
int ps5_safety_init(struct wl_event_loop *);
int ps5_safety_thread_stack(unsigned);
void ps5_safety_video(int);
void ps5_safety_close(void);
#endif
