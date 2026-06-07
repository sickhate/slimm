#define _GNU_SOURCE
#include "input.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <libinput.h>
#include <libudev.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-compose.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <sys/ioctl.h>

static int open_restricted(const char *path, int flags, void *user_data)
{
    (void)user_data;
    int fd = open(path, flags | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0)
        fprintf(stderr, "input: could not open '%s'\n", path);
    return fd;
}

static void close_restricted(int fd, void *user_data)
{
    (void)user_data;
    close(fd);
}

static const struct libinput_interface li_iface = {
    .open_restricted = open_restricted,
    .close_restricted = close_restricted,
};

int input_init(struct slimm_input *inp, int enable_numlock)
{
    memset(inp, 0, sizeof(*inp));

    struct udev *udev = udev_new();
    if (!udev) {
        fprintf(stderr, "input: udev_new failed\n");
        return -1;
    }

    inp->li = libinput_udev_create_context(&li_iface, NULL, udev);
    if (!inp->li) {
        fprintf(stderr, "input: libinput_udev_create_context failed\n");
        udev_unref(udev);
        return -1;
    }

    udev_unref(udev);

    if (libinput_udev_assign_seat(inp->li, "seat0")) {
        fprintf(stderr, "input: libinput_udev_assign_seat failed\n");
        libinput_unref(inp->li);
        return -1;
    }

    libinput_dispatch(inp->li);
    inp->fd = libinput_get_fd(inp->li);

    struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!ctx) {
        fprintf(stderr, "input: xkb_context_new failed\n");
        input_destroy(inp);
        return -1;
    }

    inp->keymap = xkb_keymap_new_from_names(ctx, NULL, 0);
    if (!inp->keymap) {
        fprintf(stderr, "input: xkb_keymap_new_from_names failed\n");
        xkb_context_unref(ctx);
        input_destroy(inp);
        return -1;
    }

    inp->xkb = xkb_state_new(inp->keymap);
    xkb_context_unref(ctx);

    if (!inp->xkb) {
        fprintf(stderr, "input: xkb_state_new failed\n");
        input_destroy(inp);
        return -1;
    }

    if (enable_numlock) {
        xkb_mod_index_t numlock_idx = xkb_keymap_mod_get_index(inp->keymap, "NumLock");
        if (numlock_idx != XKB_MOD_INVALID)
            xkb_state_update_mask(inp->xkb, 0, 0,
                                  1U << numlock_idx, 0, 0, 0);

        int tty = open("/dev/tty0", O_WRONLY);
        if (tty >= 0) {
            ioctl(tty, KDSETLED, LED_NUM);
            close(tty);
        }
    }

    return 0;
}

int input_get_fd(struct slimm_input *inp)
{
    return inp->fd;
}

int input_dispatch(struct slimm_input *inp, struct slimm_input_event *ev)
{
    memset(ev, 0, sizeof(*ev));
    libinput_dispatch(inp->li);

    struct libinput_event *le;
    while ((le = libinput_get_event(inp->li))) {
        int type = libinput_event_get_type(le);

        if (type == LIBINPUT_EVENT_KEYBOARD_KEY) {
            struct libinput_event_keyboard *ke = libinput_event_get_keyboard_event(le);
            uint32_t keycode = libinput_event_keyboard_get_key(ke) + 8;
            int pressed = libinput_event_keyboard_get_key_state(ke) == LIBINPUT_KEY_STATE_PRESSED;
            xkb_keysym_t sym = xkb_state_key_get_one_sym(inp->xkb, keycode);

            char utf[8] = {0};
            int utf_len = xkb_state_key_get_utf8(inp->xkb, keycode, utf, sizeof(utf));

            xkb_state_update_key(inp->xkb, keycode,
                                 pressed ? XKB_KEY_DOWN : XKB_KEY_UP);

            ev->type = 0;
            ev->key_sym = sym;
            ev->key_pressed = pressed;
            if (utf_len > 0 && utf_len < (int)sizeof(ev->utf8))
                memcpy(ev->utf8, utf, utf_len);
            libinput_event_destroy(le);
            return 1;
        }

        if (type == LIBINPUT_EVENT_POINTER_MOTION) {
            struct libinput_event_pointer *pe = libinput_event_get_pointer_event(le);
            ev->type = 1;
            ev->delta_x = libinput_event_pointer_get_dx(pe);
            ev->delta_y = libinput_event_pointer_get_dy(pe);
            libinput_event_destroy(le);
            return 1;
        }

        if (type == LIBINPUT_EVENT_POINTER_BUTTON) {
            struct libinput_event_pointer *pe = libinput_event_get_pointer_event(le);
            ev->type = 2;
            ev->button = libinput_event_pointer_get_button(pe);
            ev->button_pressed = libinput_event_pointer_get_button_state(pe) == LIBINPUT_BUTTON_STATE_PRESSED;
            libinput_event_destroy(le);
            return 1;
        }

        if (type == LIBINPUT_EVENT_TOUCH_DOWN) {
            struct libinput_event_touch *te = libinput_event_get_touch_event(le);
            ev->type = 3;
            ev->cursor_x = libinput_event_touch_get_x_transformed(te, 0);
            ev->cursor_y = libinput_event_touch_get_y_transformed(te, 0);
            ev->button_pressed = 1;
            ev->button = BTN_LEFT;
            libinput_event_destroy(le);
            return 1;
        }

        libinput_event_destroy(le);
    }

    return 0;
}

void input_destroy(struct slimm_input *inp)
{
    if (inp->xkb) xkb_state_unref(inp->xkb);
    if (inp->keymap) xkb_keymap_unref(inp->keymap);
    if (inp->li) libinput_unref(inp->li);
    memset(inp, 0, sizeof(*inp));
}
