#ifndef SLIMM_INPUT_H
#define SLIMM_INPUT_H

#include <stdint.h>

struct libinput;
struct xkb_state;
struct xkb_keymap;

struct slimm_input {
    struct libinput *li;
    int fd;
    struct xkb_state *xkb;
    struct xkb_keymap *keymap;
};

struct slimm_input_event {
    int type; // 0=key, 1=motion, 2=button, 3=touch
    int key_sym;
    int key_pressed;
    char utf8[8];
    int button;
    int button_pressed;
    double cursor_x, cursor_y;
    double delta_x, delta_y;
};

int input_init(struct slimm_input *inp, int enable_numlock);
int input_get_fd(struct slimm_input *inp);
int input_dispatch(struct slimm_input *inp, struct slimm_input_event *ev);
void input_destroy(struct slimm_input *inp);

#endif
