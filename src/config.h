#ifndef SLIMM_CONFIG_H
#define SLIMM_CONFIG_H

#include <stddef.h>
#include <string.h>
#include <stdint.h>

#define MAX_SESSIONS 64
#define MAX_PATH 256

struct session_entry {
    char name[64];
    char exec[MAX_PATH];
};

struct slim_color {
    float r, g, b, a;
};

struct slim_theme {
    struct slim_color bg;
    struct slim_color panel_bg;
    struct slim_color accent;
    struct slim_color text;
    struct slim_color field_bg;
    struct slim_color field_text;

    char font_name[64];
    char font_path[MAX_PATH];
    int font_size;
    int panel_width;
    int corner_radius;

    char bg_image[MAX_PATH];
    char logo_path[MAX_PATH];
    int bg_max_w;
    int bg_max_h;
};

struct slim_config {
    struct slim_theme theme;
    char default_session[MAX_PATH];
    int session_count;
    struct session_entry sessions[MAX_SESSIONS];
    char autologin_user[32];
    int autologin_delay;
    int enable_numlock;
    char mode[16];
    char sessions_dir[MAX_PATH];
};

void theme_set_defaults(struct slim_theme *t);
int config_pick_logo(struct slim_theme *t);
int config_pick_logo_id(char *id_out, size_t id_sz);
void config_init_defaults(struct slim_config *cfg);
int config_load(struct slim_config *cfg, const char *path);
void config_load_sessions(struct slim_config *cfg);
void scan_sessions(struct slim_config *cfg, const char *dir);

#endif
