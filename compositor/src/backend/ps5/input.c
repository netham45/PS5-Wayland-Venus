/* API references (read before implementing; no prospero-win code copied):
 * SDL release-2.30.x-ps5 src/video/ps5/SDL_ps5keyboard.c:134-195
 * (foreground user/Open/Init), src/joystick/ps5/SDL_ps5joystick.c:346-421
 * (standard pad Open/Init), and prospero-win native/pw_hid_ps5.c:92-120
 * (mouse sysmodule init/open, record layout in pw_hid_ps5.h:17-24).
 * HID set differencing here deliberately avoids SDL's slot-order assumption. */
#include "ps5.h"
#include "ps5log.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/interfaces/wlr_pointer.h>
struct ps5_input {
 struct wlr_backend *backend;
 struct wl_display *display;
 struct wl_event_source *timer;
 struct wlr_keyboard keyboard, pad_keyboard;
 struct wlr_pointer mouse, pad_pointer;
 int keyboard_fd, mouse_fd, pad_fd, user;
 int (*mouse_read)(int, void *, int);
 int (*mouse_close)(int);
 bool keys[256];
 uint32_t mouse_buttons, pad_buttons;
 struct ps5_pad_data previous_pad;
 unsigned polls;
};
static const struct wlr_keyboard_impl keyboard_impl = { .name = "ps5" };
static const struct wlr_pointer_impl pointer_impl = { .name = "ps5" };
/* USB HID keyboard usage -> Linux evdev, not XKB (+8 is applied by compositor). */
static const uint16_t hid_to_evdev[256] = {
 [4]=30,[5]=48,[6]=46,[7]=32,[8]=18,[9]=33,[10]=34,[11]=35,[12]=23,[13]=36,
 [14]=37,[15]=38,[16]=50,[17]=49,[18]=24,[19]=25,[20]=16,[21]=19,[22]=31,
 [23]=20,[24]=22,[25]=47,[26]=17,[27]=45,[28]=21,[29]=44,
 [30]=2,[31]=3,[32]=4,[33]=5,[34]=6,[35]=7,[36]=8,[37]=9,[38]=10,[39]=11,
 [40]=28,[41]=1,[42]=14,[43]=15,[44]=57,[45]=12,[46]=13,[47]=26,[48]=27,
 [49]=43,[50]=43,[51]=39,[52]=40,[53]=41,[54]=51,[55]=52,[56]=53,[57]=58,
 [58]=59,[59]=60,[60]=61,[61]=62,[62]=63,[63]=64,[64]=65,[65]=66,[66]=67,
 [67]=68,[68]=87,[69]=88,[70]=99,[71]=70,[72]=119,[73]=110,[74]=102,
 [75]=104,[76]=111,[77]=107,[78]=109,[79]=106,[80]=105,[81]=108,[82]=103,
 [83]=69,[84]=98,[85]=55,[86]=74,[87]=78,[88]=96,[89]=79,[90]=80,[91]=81,
 [92]=75,[93]=76,[94]=77,[95]=71,[96]=72,[97]=73,[98]=82,[99]=83,[100]=86,
 [101]=127,[102]=116,[103]=117,[104]=183,[105]=184,[106]=185,[107]=186,
 [108]=187,[109]=188,[110]=189,[111]=190,[112]=191,[113]=192,[114]=193,[115]=194,
 [224]=29,[225]=42,[226]=56,[227]=125,[228]=97,[229]=54,[230]=100,[231]=126
};
static uint32_t now_ms(void) {
 struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
 return (uint32_t)((uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000);
}
static void key(struct wlr_keyboard *keyboard, uint32_t code, bool down) {
 struct wlr_keyboard_key_event event = { .time_msec = now_ms(), .keycode = code,
 .update_state = true, .state = down ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED };
 ps5log("input key device=%s evdev=%u state=%u", keyboard->base.name, code, down);
 wlr_keyboard_notify_key(keyboard, &event);
}
static void motion(struct wlr_pointer *pointer, double dx, double dy) {
 if (dx == 0 && dy == 0) return;
 struct wlr_pointer_motion_event event = { .pointer = pointer, .time_msec = now_ms(),
 .delta_x = dx, .delta_y = dy, .unaccel_dx = dx, .unaccel_dy = dy };
 wl_signal_emit_mutable(&pointer->events.motion, &event);
}
static void buttons(struct wlr_pointer *pointer, uint32_t old, uint32_t next) {
 static const uint32_t codes[] = {272, 273, 274};
 for (unsigned i = 0; i < 3; ++i) if ((old ^ next) & (1u << i)) {
  struct wlr_pointer_button_event event = { .pointer = pointer, .time_msec = now_ms(),
  .button = codes[i], .state = next & (1u << i) ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED };
  ps5log("input pointer=%s button=%u state=%u", pointer->base.name, event.button, event.state);
  wlr_pointer_notify_button(pointer, &event);
 }
}
static void axis(struct wlr_pointer *pointer, double delta, bool horizontal, bool wheel) {
 if (delta == 0) return;
 struct wlr_pointer_axis_event event = { .pointer = pointer, .time_msec = now_ms(),
 .source = wheel ? WL_POINTER_AXIS_SOURCE_WHEEL : WL_POINTER_AXIS_SOURCE_CONTINUOUS,
 .orientation = horizontal ? WL_POINTER_AXIS_HORIZONTAL_SCROLL : WL_POINTER_AXIS_VERTICAL_SCROLL,
 .relative_direction = WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL,
 .delta = delta, .delta_discrete = wheel ? (int)(delta / 15) * 120 : 0 };
 wl_signal_emit_mutable(&pointer->events.axis, &event);
}
static uint32_t u32(const unsigned char *p) { uint32_t v; memcpy(&v, p, sizeof(v)); return v; }
static int32_t i32(const unsigned char *p) { int32_t v; memcpy(&v, p, sizeof(v)); return v; }
static uint64_t u64(const unsigned char *p) { uint64_t v; memcpy(&v, p, sizeof(v)); return v; }
static void open_devices(struct ps5_input *in) {
 if (in->keyboard_fd < 0) {
  uint64_t param = 0;
  in->keyboard_fd = sceKeyboardOpen(in->user, 0, 0, &param);
  ps5log("sceKeyboardOpen user=%#x handle=%#x", in->user, in->keyboard_fd);
 }
 if (in->pad_fd < 0) {
  in->pad_fd = scePadOpen(in->user, 0, 0, NULL);
  ps5log("scePadOpen user=%#x handle=%#x", in->user, in->pad_fd);
 }
 if (in->mouse_fd < 0) {
  int32_t handle = -1;
  int rc = sceSysmoduleLoadModule(0xa9);
  if (sceSysmoduleGetModuleHandleInternal(0xa9, &handle))
   sceSysmoduleGetModuleHandleInternal(0x800000a9, &handle);
  int (*init)(void) = NULL, (*open_mouse)(int, int, int, void *) = NULL;
  if (handle >= 0 && !sceKernelDlsym(handle, "sceMouseInit", (void **)&init) &&
   !sceKernelDlsym(handle, "sceMouseOpen", (void **)&open_mouse) &&
   !sceKernelDlsym(handle, "sceMouseRead", (void **)&in->mouse_read)) {
   sceKernelDlsym(handle, "sceMouseClose", (void **)&in->mouse_close);
   int init_rc = init(); uint64_t param = 1;
   in->mouse_fd = open_mouse(in->user, 0, 0, &param);
   if (in->mouse_fd < 0) { param = 0; in->mouse_fd = open_mouse(in->user, 0, 0, &param); }
   ps5log("sceMouseInit rc=%#x sceMouseOpen=%#x", init_rc, in->mouse_fd);
  } else ps5log("sceMouse module rc=%#x handle=%#x", rc, handle);
 }
}
static double stick(uint8_t value) {
 int delta = (int)value - 128;
 return abs(delta) <= 16 ? 0 : (delta > 0 ? delta - 16 : delta + 16) / 22.0;
}
static int poll_input(void *data) {
 struct ps5_input *in = data;
 if (in->polls++ % 1250 == 0) open_devices(in);
 if (in->keyboard_fd >= 0) {
  unsigned char state[96] = {0};
  if (sceKeyboardReadState(in->keyboard_fd, state) == 0) {
   bool next[256] = {0};
   if (state[16]) {
    int count = i32(state + 20); if (count > 16) count = 16;
    for (int i = 0; i < count; ++i) { uint16_t usage; memcpy(&usage, state + 32 + i * 2, 2); if (usage < 256 && usage > 3) next[usage] = true; }
    for (int i = 0; i < 8; ++i) next[224 + i] = (u32(state + 28) & (1u << i)) != 0;
   }
   /* SDL_ps5keyboard.c emits modifiers before ordinary usages: Shift+A in
    * one HID report must enter xkb as Shift then A, not the other way round. */
   for (int i = 224; i < 232; ++i) if (next[i] != in->keys[i])
    key(&in->keyboard, hid_to_evdev[i], next[i]);
   for (int i = 4; i < 224; ++i) if (hid_to_evdev[i] && next[i] != in->keys[i])
    key(&in->keyboard, hid_to_evdev[i], next[i]);
   memcpy(in->keys, next, sizeof(next));
  }
 }
 if (in->mouse_fd >= 0) {
  unsigned char records[64 * 40] = {0};
  int count = in->mouse_read(in->mouse_fd, records, 64);
  if (count > 64) count = 64;
  int dx = 0, dy = 0, wheel = 0, tilt = 0; uint32_t next = in->mouse_buttons; uint64_t newest = 0;
  for (int i = 0; i < count; ++i) {
   const unsigned char *r = records + i * 40;
   if (!r[8]) { if (u64(r) >= newest) { newest = u64(r); next = 0; } continue; }
   dx += i32(r + 16); dy += i32(r + 20); wheel += i32(r + 24); tilt += i32(r + 28);
   if (u64(r) >= newest) { newest = u64(r); next = u32(r + 12) & 7; }
  }
  motion(&in->mouse, dx, dy); axis(&in->mouse, -wheel * 15, false, true);
  axis(&in->mouse, tilt * 15, true, true);
  buttons(&in->mouse, in->mouse_buttons, next); in->mouse_buttons = next;
  if (dx || dy || wheel || tilt) ps5log("input mouse dx=%d dy=%d wheel=%d tilt=%d", dx, dy, wheel, tilt);
  wl_signal_emit_mutable(&in->mouse.events.frame, &in->mouse);
 }
 if (in->pad_fd >= 0) {
  struct ps5_pad_data pad = {0};
  if (scePadReadState(in->pad_fd, &pad) == 0) {
   if (!pad.connected) { pad.buttons = 0; pad.lx = pad.ly = pad.rx = pad.ry = 128; }
   double dx = stick(pad.lx), dy = stick(pad.ly);
   if (pad.touch.fingers && in->previous_pad.touch.fingers &&
    pad.touch.touch[0].finger == in->previous_pad.touch.touch[0].finger) {
    dx += ((int)pad.touch.touch[0].x - in->previous_pad.touch.touch[0].x) * 1.5;
    dy += ((int)pad.touch.touch[0].y - in->previous_pad.touch.touch[0].y) * 1.5;
   }
   motion(&in->pad_pointer, dx, dy);
   axis(&in->pad_pointer, stick(pad.ry), false, false);
   axis(&in->pad_pointer, stick(pad.rx), true, false);
   uint32_t next = ((pad.buttons & (0x4000 | 0x0200)) || pad.r2 > 127 ? 1 : 0) |
    ((pad.buttons & (0x2000 | 0x0100)) || pad.l2 > 127 ? 2 : 0);
   buttons(&in->pad_pointer, in->pad_buttons, next); in->pad_buttons = next;
   bool options = pad.buttons & 8, previous = in->previous_pad.buttons & 8;
   if (options != previous) { key(&in->pad_keyboard, 125, options); if (options) { key(&in->pad_keyboard, 15, true); key(&in->pad_keyboard, 15, false); } }
   /* Share is bit zero in the ScePad button mask; Options+Share exits. */
   if ((pad.buttons & 9) == 9) { ps5log("Options+Share shutdown"); wl_display_terminate(in->display); }
   if ((dx || dy) && in->polls % 25 == 0) ps5log("input DualSense dx=%.2f dy=%.2f", dx, dy);
   in->previous_pad = pad;
   wl_signal_emit_mutable(&in->pad_pointer.events.frame, &in->pad_pointer);
  }
 }
 wl_event_source_timer_update(in->timer, 4);
 return 0;
}
struct ps5_input *ps5_input_create(struct wlr_backend *backend, struct wl_display *display) {
 struct ps5_input *in = calloc(1, sizeof(*in)); if (!in) return NULL;
 in->backend = backend; in->display = display;
 in->keyboard_fd = in->mouse_fd = in->pad_fd = -1; in->user = 0xff;
 ps5log("sceUserServiceInitialize rc=%#x", sceUserServiceInitialize(NULL));
 int rc = sceUserServiceGetForegroundUser(&in->user);
 ps5log("input foreground user=%#x rc=%#x keyboard_init=%#x pad_init=%#x", in->user, rc, sceKeyboardInit(), scePadInit());
 wlr_keyboard_init(&in->keyboard, &keyboard_impl, "PS5 USB/Bluetooth keyboard");
 wlr_keyboard_init(&in->pad_keyboard, &keyboard_impl, "PS5 DualSense keys");
 wlr_pointer_init(&in->mouse, &pointer_impl, "PS5 USB/Bluetooth mouse");
 wlr_pointer_init(&in->pad_pointer, &pointer_impl, "PS5 DualSense pointer");
 in->timer = wl_event_loop_add_timer(wl_display_get_event_loop(display), poll_input, in);
 if (!in->timer) { ps5_input_destroy(in); return NULL; }
 return in;
}
void ps5_input_start(struct ps5_input *in) {
 wl_signal_emit_mutable(&in->backend->events.new_input, &in->keyboard.base);
 wl_signal_emit_mutable(&in->backend->events.new_input, &in->pad_keyboard.base);
 wl_signal_emit_mutable(&in->backend->events.new_input, &in->mouse.base);
 wl_signal_emit_mutable(&in->backend->events.new_input, &in->pad_pointer.base);
 ps5log("input enumerated keyboard mouse DualSense-keyboard DualSense-pointer poll=250Hz");
 wl_event_source_timer_update(in->timer, 4);
}
void ps5_input_destroy(struct ps5_input *in) {
 if (!in) return;
 if (in->timer) wl_event_source_remove(in->timer);
 if (in->keyboard_fd >= 0) sceKeyboardClose(in->keyboard_fd);
 if (in->mouse_fd >= 0 && in->mouse_close) in->mouse_close(in->mouse_fd);
 if (in->pad_fd >= 0) scePadClose(in->pad_fd);
 wlr_keyboard_finish(&in->keyboard); wlr_keyboard_finish(&in->pad_keyboard);
 wlr_pointer_finish(&in->mouse); wlr_pointer_finish(&in->pad_pointer);
 free(in);
}
