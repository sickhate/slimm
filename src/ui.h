#ifndef SLIMM_UI_H
#define SLIMM_UI_H

#include <time.h>
#include "config.h"
#include "font.h"
#include "theme.h"
#include "renderer.h"

enum ui_action {
    UI_NONE = 0,
    UI_LOGIN,
    UI_QUIT,
    UI_POWEROFF,
    UI_REBOOT,
    UI_SUSPEND,
};

enum input_mode {
    INPUT_USERNAME = 0,
    INPUT_PASSWORD,
};

struct ui_state {
    int input_mode;
    char username[64];
    char password[64];
    int username_len;
    int password_len;
    int cursor_visible;
    int cursor_toggle;

    int screen_w, screen_h;

    int selected_session;
    int session_count;
    struct session_entry *sessions;
    int dropdown_open;
    int dropdown_hover;
    int dropdown_scroll;

    int autologin_active;
    struct timespec autologin_start;
    int autologin_delay;

    char message[128];
    struct slim_theme *theme;
    struct slim_images *images;

    struct slim_font *font;

    uint32_t panel_fbo_tex;
    int panel_fbo_w, panel_fbo_h;

    uint32_t field_cache_tex;
    uint32_t field_cache_fbo;
    int field_dirty;
    int cursor_x_rel;
    int cursor_y_rel;
};

void ui_init(struct ui_state *ui, struct slim_config *cfg,
             struct slim_images *images, struct slim_font *font);
void ui_init_fbo(struct ui_state *ui, struct slim_renderer *r);
void ui_render(struct ui_state *ui, struct slim_renderer *r);
void ui_destroy_fbo(struct ui_state *ui);
enum ui_action ui_key(struct ui_state *ui, int sym, int pressed, const char *utf8);

#endif
