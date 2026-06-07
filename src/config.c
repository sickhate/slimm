#include "config.h"
#include "ste2.h"
#include "exec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <ctype.h>

void theme_set_defaults(struct slim_theme *t)
{
    memset(t, 0, sizeof(*t));
    t->bg        = (struct slim_color){0.08, 0.08, 0.10, 1.0};
    t->panel_bg  = (struct slim_color){0.16, 0.16, 0.18, 0.94};
    t->accent    = (struct slim_color){0.0, 0.75, 1.0, 1.0};
    t->text      = (struct slim_color){1.0, 1.0, 1.0, 0.9};
    t->field_bg  = (struct slim_color){0.20, 0.20, 0.22, 1.0};
    t->field_text = (struct slim_color){1.0, 1.0, 1.0, 0.9};
    strcpy(t->font_name, "JetBrainsMono Nerd Font");
    t->font_path[0] = '\0';
    t->font_size = 18;
    t->panel_width = 500;
    t->corner_radius = 16;
    t->bg_max_w = STE2_BG_MAX_W;
    t->bg_max_h = STE2_BG_MAX_H;
}

static int parse_color(const char *str, struct slim_color *c)
{
    if (str[0] != '#') return -1;
    unsigned int r, g, b, a = 255;
    if (strlen(str) == 7)
        sscanf(str + 1, "%02x%02x%02x", &r, &g, &b);
    else if (strlen(str) == 9)
        sscanf(str + 1, "%02x%02x%02x%02x", &r, &g, &b, &a);
    else
        return -1;
    c->r = r / 255.0f;
    c->g = g / 255.0f;
    c->b = b / 255.0f;
    c->a = a / 255.0f;
    return 0;
}

static void trim(char *s)
{
    char *e = s + strlen(s) - 1;
    while (e >= s && (*e == ' ' || *e == '\t' || *e == '\r' || *e == '\n')) *e-- = '\0';
    char *p = s;
    while (*p == ' ' || *p == '\t') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
}

static void strip_quotes(char *s)
{
    size_t len = strlen(s);
    if (len >= 2 && ((s[0] == '"' && s[len-1] == '"') ||
                     (s[0] == '\'' && s[len-1] == '\''))) {
        memmove(s, s + 1, len - 2);
        s[len - 2] = '\0';
    }
}

static int try_logo_id(const char *id, struct slim_theme *t)
{
    if (!id || !id[0])
        return 0;

    static const char *dirs[] = {
        "logos/",
        "/usr/share/slimm/logos/",
        "/usr/share/slim2/logos/",
        NULL,
    };
    for (int i = 0; dirs[i]; i++) {
        char path[MAX_PATH];
        snprintf(path, sizeof(path), "%s%s.png", dirs[i], id);
        if (access(path, R_OK) == 0) {
            strncpy(t->logo_path, path, sizeof(t->logo_path) - 1);
            return 1;
        }
    }
    return 0;
}

static int read_os_release(char *id, size_t id_sz, char *id_like, size_t like_sz)
{
    const char *paths[] = { "/etc/os-release", "/usr/lib/os-release", NULL };
    FILE *f = NULL;

    id[0] = id_like[0] = '\0';
    for (int i = 0; paths[i]; i++) {
        f = fopen(paths[i], "r");
        if (f)
            break;
    }
    if (!f)
        return -1;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = '\0';
        char *val = eq + 1;
        trim(line);
        trim(val);
        strip_quotes(val);
        if (strcmp(line, "ID") == 0)
            strncpy(id, val, id_sz - 1);
        else if (strcmp(line, "ID_LIKE") == 0)
            strncpy(id_like, val, like_sz - 1);
    }
    fclose(f);
    return id[0] ? 0 : -1;
}

int config_pick_logo_id(char *id_out, size_t id_sz)
{
    struct slim_theme t;
    theme_set_defaults(&t);
    if (!config_pick_logo(&t))
        return -1;

    const char *base = strrchr(t.logo_path, '/');
    base = base ? base + 1 : t.logo_path;
    size_t len = strlen(base);
    if (len < 5 || strcmp(base + len - 4, ".png") != 0)
        return -1;

    len -= 4;
    if (len >= id_sz)
        len = id_sz - 1;
    memcpy(id_out, base, len);
    id_out[len] = '\0';
    return 0;
}

int config_pick_logo(struct slim_theme *t)
{
    if (t->logo_path[0])
        return 1;

    char id[64] = {0}, id_like[256] = {0};
    if (read_os_release(id, sizeof(id), id_like, sizeof(id_like)) < 0)
        return 0;

    if (try_logo_id(id, t))
        return 1;

    for (char *p = id_like; *p; ) {
        while (*p == ' ' || *p == '\t')
            p++;
        if (!*p)
            break;
        char token[64];
        size_t n = 0;
        while (*p && *p != ' ' && *p != '\t' && n + 1 < sizeof(token))
            token[n++] = *p++;
        token[n] = '\0';
        if (token[0] && try_logo_id(token, t))
            return 1;
    }

    return 0;
}

void scan_sessions(struct slim_config *cfg, const char *dir)
{
    DIR *d = opendir(dir);
    if (!d) return;

    struct dirent *e;
    while ((e = readdir(d)) && cfg->session_count < MAX_SESSIONS) {
        char *dot = strrchr(e->d_name, '.');
        if (!dot || strcmp(dot, ".desktop") != 0) continue;

        char path[512];
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);

        FILE *f = fopen(path, "r");
        if (!f) continue;

        char line[256];
        char name[64] = {0}, exec[256] = {0};
        while (fgets(line, sizeof(line), f)) {
            if (sscanf(line, "Name=%63[^\n]", name) == 1) continue;
            if (sscanf(line, "Exec=%255[^\n]", exec) == 1) continue;
        }
        fclose(f);

        if (name[0] && exec[0]) {
            exec_sanitize_desktop(exec);
            strncpy(cfg->sessions[cfg->session_count].name, name,
                    sizeof(cfg->sessions[cfg->session_count].name) - 1);
            strncpy(cfg->sessions[cfg->session_count].exec, exec,
                    sizeof(cfg->sessions[cfg->session_count].exec) - 1);
            cfg->session_count++;
        }
    }
    closedir(d);
}

int config_load(struct slim_config *cfg, const char *path)
{
    memset(cfg, 0, sizeof(*cfg));
    theme_set_defaults(&cfg->theme);
    strcpy(cfg->default_session, "Hyprland");
    strcpy(cfg->mode, "drm");

    FILE *f = fopen(path, "r");
    if (!f) goto done;

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\0') continue;
        p[strcspn(p, "\r\n")] = '\0';

        char key[64], val[256];
        if (sscanf(p, "%63s = %255[^\n]", key, val) < 2) continue;
        trim(val);
        strip_quotes(val);

        struct slim_theme *t = &cfg->theme;

        if      (strcmp(key, "background_color") == 0)   parse_color(val, &t->bg);
        else if (strcmp(key, "panel_bg_color") == 0)     parse_color(val, &t->panel_bg);
        else if (strcmp(key, "accent_color") == 0)       parse_color(val, &t->accent);
        else if (strcmp(key, "text_color") == 0)         parse_color(val, &t->text);
        else if (strcmp(key, "field_bg_color") == 0)     parse_color(val, &t->field_bg);
        else if (strcmp(key, "field_text_color") == 0)   parse_color(val, &t->field_text);
        else if (strcmp(key, "font") == 0)               strncpy(t->font_name, val, sizeof(t->font_name) - 1);
        else if (strcmp(key, "font_path") == 0)          strncpy(t->font_path, val, sizeof(t->font_path) - 1);
        else if (strcmp(key, "font_size") == 0)          t->font_size = atoi(val);
        else if (strcmp(key, "panel_width") == 0)        t->panel_width = atoi(val);
        else if (strcmp(key, "corner_radius") == 0)      t->corner_radius = atoi(val);
        else if (strcmp(key, "background_image") == 0)   strncpy(t->bg_image, val, sizeof(t->bg_image) - 1);
        else if (strcmp(key, "logo_path") == 0)          strncpy(t->logo_path, val, sizeof(t->logo_path) - 1);
        else if (strcmp(key, "bg_max_width") == 0)       t->bg_max_w = atoi(val);
        else if (strcmp(key, "bg_max_height") == 0)      t->bg_max_h = atoi(val);
        else if (strcmp(key, "default_session") == 0)    strncpy(cfg->default_session, val, sizeof(cfg->default_session) - 1);
        else if (strcmp(key, "autologin_user") == 0)     strncpy(cfg->autologin_user, val, sizeof(cfg->autologin_user) - 1);
        else if (strcmp(key, "autologin_delay") == 0)    cfg->autologin_delay = atoi(val);
        else if (strcmp(key, "numlock") == 0)            cfg->enable_numlock = (strcmp(val, "true") == 0 || strcmp(val, "1") == 0);
        else if (strcmp(key, "mode") == 0)               strncpy(cfg->mode, val, sizeof(cfg->mode) - 1);
        else if (strcmp(key, "sessions_dir") == 0)       strncpy(cfg->sessions_dir, val, sizeof(cfg->sessions_dir) - 1);
        else if (strcmp(key, "session") == 0) {
            char *sep = strchr(val, '|');
            if (sep) {
                *sep = '\0';
                if (cfg->session_count < MAX_SESSIONS) {
                    strncpy(cfg->sessions[cfg->session_count].name, val,
                            sizeof(cfg->sessions[cfg->session_count].name) - 1);
                    strncpy(cfg->sessions[cfg->session_count].exec, sep + 1,
                            sizeof(cfg->sessions[cfg->session_count].exec) - 1);
                    cfg->session_count++;
                }
            }
        }
    }
    fclose(f);

done:
    if (cfg->session_count == 0) {
        if (cfg->sessions_dir[0])
            scan_sessions(cfg, cfg->sessions_dir);
        scan_sessions(cfg, "/usr/share/wayland-sessions");
    }

    if (cfg->session_count == 0) {
        strcpy(cfg->sessions[0].name, "Hyprland");
        strcpy(cfg->sessions[0].exec, "/usr/bin/Hyprland");
        cfg->session_count = 1;
    }

    return 0;
}

void config_init_defaults(struct slim_config *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    theme_set_defaults(&cfg->theme);
    strcpy(cfg->default_session, "Hyprland");
    strcpy(cfg->mode, "drm");
}

void config_load_sessions(struct slim_config *cfg)
{
    if (cfg->session_count > 0) return;
    if (cfg->sessions_dir[0])
        scan_sessions(cfg, cfg->sessions_dir);
    scan_sessions(cfg, "/usr/share/wayland-sessions");
    if (cfg->session_count == 0) {
        strcpy(cfg->sessions[0].name, "Hyprland");
        strcpy(cfg->sessions[0].exec, "/usr/bin/Hyprland");
        cfg->session_count = 1;
    }
}
