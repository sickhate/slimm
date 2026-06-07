#ifndef SLIMM_WAYLAND_H
#define SLIMM_WAYLAND_H

#include <stdint.h>

struct wl_display;
struct wl_compositor;
struct wl_surface;
struct wl_seat;
struct wl_keyboard;
struct wl_egl_window;
struct zwlr_layer_shell_v1;
struct zwlr_layer_surface_v1;
struct xkb_state;
struct xkb_keymap;

#define WAYLAND_KEY_QUEUE_SIZE 4

struct slim_wayland_event {
    int key_sym;
    int key_pressed;
    char utf8[8];
};

struct slim_wayland {
    struct wl_display          *display;
    struct wl_compositor       *compositor;
    struct wl_surface          *surface;
    struct zwlr_layer_shell_v1 *layer_shell;
    struct zwlr_layer_surface_v1 *layer_surface;
    struct wl_egl_window       *egl_window;
    struct wl_seat             *seat;
    struct wl_keyboard         *keyboard;
    struct xkb_state           *xkb;
    struct xkb_keymap          *keymap;

    int width, height;
    int closed;
    int configured;

    struct slim_wayland_event key_queue[WAYLAND_KEY_QUEUE_SIZE];
    int key_head;
    int key_tail;
};

int  wayland_connect(struct slim_wayland *wl);
int  wayland_create_surface(struct slim_wayland *wl);
void wayland_destroy(struct slim_wayland *wl);
int  wayland_get_fd(struct slim_wayland *wl);
int  wayland_dispatch(struct slim_wayland *wl, struct slim_wayland_event *ev);
void wayland_flush(struct slim_wayland *wl);

#endif
