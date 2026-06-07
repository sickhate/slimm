#define _GNU_SOURCE
#include "wayland.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon.h>
#include "wlr-layer-shell-unstable-v1.h"

/* Stub for xdg_popup — we don't call get_popup at runtime */
const struct wl_interface xdg_popup_interface = { "xdg_popup", 1, 0, NULL, 0, NULL };

static void handle_configure(void *data, struct zwlr_layer_surface_v1 *lsurf,
                             uint32_t serial, uint32_t w, uint32_t h)
{
    struct slim_wayland *wl = data;
    zwlr_layer_surface_v1_ack_configure(lsurf, serial);
    wl->configured = 1;
    if (w > 0 && h > 0 && (w != (uint32_t)wl->width || h != (uint32_t)wl->height)) {
        wl->width = w;
        wl->height = h;
        if (wl->egl_window)
            wl_egl_window_resize(wl->egl_window, w, h, 0, 0);
    }
}

static void handle_closed(void *data, struct zwlr_layer_surface_v1 *lsurf)
{
    (void)lsurf;
    struct slim_wayland *wl = data;
    wl->closed = 1;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = handle_configure,
    .closed = handle_closed,
};

static void keyboard_keymap(void *data, struct wl_keyboard *kb,
                             uint32_t fmt, int32_t fd, uint32_t size)
{
    struct slim_wayland *wl = data;
    (void)kb;

    if (fmt != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close(fd);
        return;
    }

    char *map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED) {
        close(fd);
        return;
    }

    struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (ctx) {
        if (wl->keymap) xkb_keymap_unref(wl->keymap);
        if (wl->xkb) xkb_state_unref(wl->xkb);
        wl->keymap = xkb_keymap_new_from_string(ctx, map,
                        XKB_KEYMAP_FORMAT_TEXT_V1, 0);
        if (wl->keymap)
            wl->xkb = xkb_state_new(wl->keymap);
        xkb_context_unref(ctx);
    }

    munmap(map, size);
    close(fd);
}

static void keyboard_enter(void *data, struct wl_keyboard *kb,
                            uint32_t serial, struct wl_surface *surf,
                            struct wl_array *keys)
{
    (void)data; (void)kb; (void)serial; (void)surf; (void)keys;
}

static void keyboard_leave(void *data, struct wl_keyboard *kb,
                            uint32_t serial, struct wl_surface *surf)
{
    (void)data; (void)kb; (void)serial; (void)surf;
}

static void keyboard_key(void *data, struct wl_keyboard *kb,
                          uint32_t serial, uint32_t time,
                          uint32_t key, uint32_t state)
{
    struct slim_wayland *wl = data;
    (void)kb; (void)serial; (void)time;

    uint32_t keycode = key + 8;
    int pressed = state == WL_KEYBOARD_KEY_STATE_PRESSED;

    if (!wl->xkb || !wl->keymap) return;

    xkb_keysym_t sym = xkb_state_key_get_one_sym(wl->xkb, keycode);
    char utf[8] = {0};
    xkb_state_key_get_utf8(wl->xkb, keycode, utf, sizeof(utf));
    xkb_state_update_key(wl->xkb, keycode,
                         pressed ? XKB_KEY_DOWN : XKB_KEY_UP);

    int next = (wl->key_head + 1) % WAYLAND_KEY_QUEUE_SIZE;
    if (next != wl->key_tail) {
        wl->key_queue[wl->key_head].key_sym = sym;
        wl->key_queue[wl->key_head].key_pressed = pressed;
        memset(wl->key_queue[wl->key_head].utf8, 0, 8);
        memcpy(wl->key_queue[wl->key_head].utf8, utf, 7);
        wl->key_head = next;
    }
}

static void keyboard_modifiers(void *data, struct wl_keyboard *kb,
                                uint32_t serial, uint32_t mods_depressed,
                                uint32_t mods_latched, uint32_t mods_locked,
                                uint32_t group)
{
    struct slim_wayland *wl = data;
    (void)kb; (void)serial;
    if (!wl->xkb) return;
    xkb_state_update_mask(wl->xkb, mods_depressed, mods_latched,
                          mods_locked, group, 0, 0);
}

static void keyboard_repeat_info(void *data, struct wl_keyboard *kb,
                                  int32_t rate, int32_t delay)
{
    (void)data; (void)kb; (void)rate; (void)delay;
}

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_keymap,
    .enter = keyboard_enter,
    .leave = keyboard_leave,
    .key = keyboard_key,
    .modifiers = keyboard_modifiers,
    .repeat_info = keyboard_repeat_info,
};

static void seat_capabilities(void *data, struct wl_seat *seat,
                               uint32_t caps)
{
    struct slim_wayland *wl = data;
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !wl->keyboard) {
        wl->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(wl->keyboard, &keyboard_listener, wl);
    } else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && wl->keyboard) {
        wl_keyboard_destroy(wl->keyboard);
        wl->keyboard = NULL;
    }
}

static void seat_name(void *data, struct wl_seat *seat, const char *name)
{
    (void)data; (void)seat; (void)name;
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name,
};

static void handle_global(void *data, struct wl_registry *registry,
                          uint32_t name, const char *iface, uint32_t ver)
{
    struct slim_wayland *wl = data;
    (void)ver;

    if (strcmp(iface, "wl_compositor") == 0) {
        wl->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
    } else if (strcmp(iface, "zwlr_layer_shell_v1") == 0) {
        wl->layer_shell = wl_registry_bind(registry, name,
                                           &zwlr_layer_shell_v1_interface, 1);
    } else if (strcmp(iface, "wl_seat") == 0) {
        wl->seat = wl_registry_bind(registry, name, &wl_seat_interface, 7);
        wl_seat_add_listener(wl->seat, &seat_listener, wl);
    }
}

static void handle_global_remove(void *data, struct wl_registry *registry,
                                 uint32_t name)
{
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = handle_global,
    .global_remove = handle_global_remove,
};

int wayland_connect(struct slim_wayland *wl)
{
    memset(wl, 0, sizeof(*wl));

    wl->display = wl_display_connect(NULL);
    if (!wl->display) {
        fprintf(stderr, "wayland: wl_display_connect failed\n");
        return -1;
    }

    struct wl_registry *registry = wl_display_get_registry(wl->display);
    wl_registry_add_listener(registry, &registry_listener, wl);
    wl_display_roundtrip(wl->display);
    wl_display_roundtrip(wl->display);
    wl_registry_destroy(registry);

    if (!wl->compositor) {
        fprintf(stderr, "wayland: no wl_compositor\n");
        wayland_destroy(wl);
        return -1;
    }
    if (!wl->layer_shell) {
        fprintf(stderr, "wayland: no zwlr_layer_shell_v1\n");
        wayland_destroy(wl);
        return -1;
    }

    return 0;
}

int wayland_create_surface(struct slim_wayland *wl)
{
    wl->surface = wl_compositor_create_surface(wl->compositor);
    if (!wl->surface) {
        fprintf(stderr, "wayland: wl_compositor_create_surface failed\n");
        return -1;
    }

    wl->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        wl->layer_shell, wl->surface, NULL,
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "slimm");
    if (!wl->layer_surface) {
        fprintf(stderr, "wayland: get_layer_surface failed\n");
        return -1;
    }

    zwlr_layer_surface_v1_set_anchor(wl->layer_surface,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_exclusive_zone(wl->layer_surface, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(
        wl->layer_surface, true);
    zwlr_layer_surface_v1_add_listener(wl->layer_surface,
                                       &layer_surface_listener, wl);

    wl_surface_commit(wl->surface);
    wl_display_roundtrip(wl->display);

    if (!wl->configured) {
        fprintf(stderr, "wayland: surface not configured\n");
        return -1;
    }

    wl->egl_window = wl_egl_window_create(wl->surface, wl->width, wl->height);
    if (!wl->egl_window) {
        fprintf(stderr, "wayland: wl_egl_window_create failed\n");
        return -1;
    }

    return 0;
}

void wayland_destroy(struct slim_wayland *wl)
{
    if (wl->keyboard) wl_keyboard_destroy(wl->keyboard);
    if (wl->seat) wl_seat_destroy(wl->seat);
    if (wl->egl_window) wl_egl_window_destroy(wl->egl_window);
    if (wl->layer_surface) zwlr_layer_surface_v1_destroy(wl->layer_surface);
    if (wl->surface) wl_surface_destroy(wl->surface);
    if (wl->layer_shell) zwlr_layer_shell_v1_destroy(wl->layer_shell);
    if (wl->compositor) wl_compositor_destroy(wl->compositor);
    if (wl->xkb) xkb_state_unref(wl->xkb);
    if (wl->keymap) xkb_keymap_unref(wl->keymap);
    if (wl->display) wl_display_disconnect(wl->display);
    memset(wl, 0, sizeof(*wl));
}

int wayland_get_fd(struct slim_wayland *wl)
{
    return wl_display_get_fd(wl->display);
}

int wayland_dispatch(struct slim_wayland *wl, struct slim_wayland_event *ev)
{
    if (wl->key_tail != wl->key_head) {
        *ev = wl->key_queue[wl->key_tail];
        wl->key_tail = (wl->key_tail + 1) % WAYLAND_KEY_QUEUE_SIZE;
        return 1;
    }
    return 0;
}

void wayland_flush(struct slim_wayland *wl)
{
    wl_display_flush(wl->display);
}
