#define _GNU_SOURCE
#include "ui.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon.h>
#include <GLES3/gl3.h>

#define PANEL_PAD 24
#define FIELD_PAD 16
#define FIELD_HEIGHT 48
#define SESSION_HEIGHT 36
#define FIELD_GAP 12
#define LOGO_GAP 32
#define LOGO_OFFSET_Y 8   /* nudge below panel top padding */
#define FOOTER_MARGIN 20
#define DD_PAD 6
#define DD_GAP 2
#define DD_MAX_VISIBLE 6

#ifndef SLIMM_VERSION
#define SLIMM_VERSION "dev"
#endif

static struct slim_color field_placeholder(struct slim_color c)
{
    return (struct slim_color){ c.r * 0.45f, c.g * 0.45f, c.b * 0.45f, c.a };
}

static void draw_text(struct slim_renderer *r, struct slim_font *font,
                      const char *text, float x, float y,
                      struct slim_color color)
{
    while (*text) {
        struct glyph_info *g = font_get_glyph(font, (unsigned char)*text);
        if (!g) { text++; continue; }
        float gx = x + g->bearing_x;
        float gy = y - g->bearing_y;
        renderer_draw_glyph(r, gx, gy, g->width, g->height,
                           font->atlas_tex,
                           g->u0, g->v0, g->u1, g->v1, color);
        x += g->advance_x;
        text++;
    }
}

static void draw_text_centered(struct slim_renderer *r, struct slim_font *font,
                               const char *text, float cx, float y,
                               struct slim_color color)
{
    int w = font_text_width(font, text);
    draw_text(r, font, text, cx - w / 2, y, color);
}

void ui_init(struct ui_state *ui, struct slim_config *cfg,
             struct slim_images *images, struct slim_font *font)
{
    memset(ui, 0, sizeof(*ui));
    ui->theme = &cfg->theme;
    ui->images = images;
    ui->font = font;
    ui->sessions = cfg->sessions;
    ui->session_count = cfg->session_count;
    ui->cursor_visible = 1;
    ui->field_dirty = 1;

    for (int i = 0; i < cfg->session_count; i++) {
        if (strcmp(cfg->sessions[i].name, cfg->default_session) == 0) {
            ui->selected_session = i;
            break;
        }
    }

    if (cfg->autologin_user[0]) {
        strncpy(ui->username, cfg->autologin_user, sizeof(ui->username) - 1);
        ui->username_len = strlen(ui->username);
        ui->input_mode = INPUT_PASSWORD;
        if (cfg->autologin_delay > 0) {
            ui->autologin_active = 1;
            clock_gettime(CLOCK_MONOTONIC, &ui->autologin_start);
        }
    }
}

static void ui_render_field(struct ui_state *ui, struct slim_renderer *r)
{
    struct slim_theme *t = ui->theme;
    int fw = t->panel_width - PANEL_PAD * 2;
    int fh = FIELD_HEIGHT;
    int font_h = ui->font->pixel_size;
    int baseline_y = (fh - font_h) / 2 + font_h;

    glBindFramebuffer(GL_FRAMEBUFFER, ui->field_cache_fbo);
    renderer_set_viewport(r, fw, fh);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    renderer_draw_rounded_rect(r, 0, 0, fw, fh,
                               t->corner_radius, t->field_bg);

    if (ui->input_mode == INPUT_USERNAME) {
        if (ui->username_len > 0) {
            draw_text(r, ui->font, ui->username, FIELD_PAD,
                      baseline_y, t->field_text);
        } else {
            draw_text(r, ui->font, "username", FIELD_PAD,
                      baseline_y, field_placeholder(t->field_text));
        }
    } else {
        if (ui->password_len > 0) {
            char dots[128];
            int dlen = ui->password_len < 127 ? ui->password_len : 127;
            memset(dots, '*', dlen);
            dots[dlen] = '\0';
            draw_text(r, ui->font, dots, FIELD_PAD,
                      baseline_y, t->field_text);
        } else {
            draw_text(r, ui->font, "password", FIELD_PAD,
                      baseline_y, field_placeholder(t->field_text));
        }
    }

    if (ui->input_mode == INPUT_USERNAME) {
        int tw = font_text_width(ui->font, ui->username);
        ui->cursor_x_rel = FIELD_PAD + tw;
    } else {
        int tw = ui->password_len * (int)(font_h * 0.65f);
        ui->cursor_x_rel = FIELD_PAD + tw;
    }
    ui->cursor_y_rel = (fh - font_h) / 2;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    renderer_set_viewport(r, ui->screen_w, ui->screen_h);
    ui->field_dirty = 0;
}

void ui_init_fbo(struct ui_state *ui, struct slim_renderer *r)
{
    struct slim_theme *t = ui->theme;
    int pw = t->panel_width;

    int ph = PANEL_PAD + FIELD_HEIGHT + FIELD_GAP + SESSION_HEIGHT + PANEL_PAD;

    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, pw, ph, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, tex, 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
        fprintf(stderr, "ui: panel FBO incomplete (0x%x)\n", status);

    ui->panel_fbo_tex = tex;
    ui->panel_fbo_w = pw;
    ui->panel_fbo_h = ph;

    renderer_set_viewport(r, pw, ph);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    renderer_draw_rounded_rect(r, 0, 0, pw, ph,
                               t->corner_radius, t->panel_bg);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    renderer_set_viewport(r, ui->screen_w, ui->screen_h);
    glDeleteFramebuffers(1, &fbo);

    int fw = pw - PANEL_PAD * 2;
    glGenFramebuffers(1, &ui->field_cache_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, ui->field_cache_fbo);

    glGenTextures(1, &ui->field_cache_tex);
    glBindTexture(GL_TEXTURE_2D, ui->field_cache_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, fw, FIELD_HEIGHT, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, ui->field_cache_tex, 0);

    status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
        fprintf(stderr, "ui: field cache FBO incomplete (0x%x)\n", status);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    ui_render_field(ui, r);
}

void ui_destroy_fbo(struct ui_state *ui)
{
    if (ui->panel_fbo_tex) {
        glDeleteTextures(1, &ui->panel_fbo_tex);
        ui->panel_fbo_tex = 0;
    }
    if (ui->field_cache_tex) {
        glDeleteTextures(1, &ui->field_cache_tex);
        ui->field_cache_tex = 0;
    }
    if (ui->field_cache_fbo) {
        glDeleteFramebuffers(1, &ui->field_cache_fbo);
        ui->field_cache_fbo = 0;
    }
}

void ui_render(struct ui_state *ui, struct slim_renderer *r)
{
    struct slim_theme *t = ui->theme;
    int pw = t->panel_width;
    int px = (ui->screen_w - pw) / 2;
    int fw = pw - PANEL_PAD * 2;

    int logo_h = (ui->images && ui->images->has_logo) ? ui->images->logo_h : 0;
    int panel_h = PANEL_PAD + FIELD_HEIGHT + FIELD_GAP + SESSION_HEIGHT + PANEL_PAD;
    int total_h = panel_h;
    if (logo_h > 0)
        total_h += PANEL_PAD + LOGO_OFFSET_Y + logo_h + LOGO_GAP;

    int start_y = (ui->screen_h - total_h) / 2;
    if (start_y < 0) start_y = 0;

    int panel_y = start_y;
    int logo_y = start_y + PANEL_PAD + LOGO_OFFSET_Y;
    if (logo_h > 0)
        panel_y = logo_y + logo_h + LOGO_GAP;

    int field_y = panel_y + PANEL_PAD;
    int session_y = field_y + FIELD_HEIGHT + FIELD_GAP;

    if (ui->field_dirty)
        ui_render_field(ui, r);

    if (ui->images && ui->images->has_bg) {
        renderer_draw_texture(r, 0, 0, ui->screen_w, ui->screen_h,
                              ui->images->bg_tex, (struct slim_color){1,1,1,1});
    } else {
        renderer_clear(r, t->bg);
    }

    if (ui->images && ui->images->has_logo) {
        float lx = px + (pw - ui->images->logo_w) / 2.0f;
        renderer_draw_texture(r, lx, (float)logo_y,
                              ui->images->logo_w, ui->images->logo_h,
                              ui->images->logo_tex, (struct slim_color){1,1,1,1});
    }

    renderer_draw_texture_rt(r, px, panel_y, pw, panel_h,
                             ui->panel_fbo_tex, (struct slim_color){1,1,1,1});

    renderer_draw_texture_rt(r, px + PANEL_PAD, field_y, fw, FIELD_HEIGHT,
                             ui->field_cache_tex, (struct slim_color){1,1,1,1});

    int font_h = ui->font->pixel_size;
    int session_baseline_y = session_y + (SESSION_HEIGHT - font_h) / 2 + font_h;

    {
        char sess_label[128];
        if (ui->selected_session < ui->session_count)
            snprintf(sess_label, sizeof(sess_label), "%s",
                     ui->sessions[ui->selected_session].name);
        else
            snprintf(sess_label, sizeof(sess_label), "Default");
        draw_text(r, ui->font, sess_label, px + PANEL_PAD + 8,
                  session_baseline_y,
                  (struct slim_color){0.7, 0.7, 0.7, 1});
    }

    if (ui->cursor_visible) {
        int cw = 2;
        int cx_pos = px + PANEL_PAD + ui->cursor_x_rel;
        int cy_pos = field_y + ui->cursor_y_rel;
        if ((ui->input_mode == INPUT_USERNAME && ui->username_len < 63) ||
            (ui->input_mode == INPUT_PASSWORD && ui->password_len < 63)) {
            renderer_draw_rect(r, cx_pos, cy_pos, cw, font_h,
                               (struct slim_color){1,1,1,0.7});
        }
    }

    if (ui->message[0]) {
        draw_text_centered(r, ui->font, ui->message,
                           ui->screen_w / 2,
                           session_y + SESSION_HEIGHT + 12,
                           (struct slim_color){1, 0.3, 0.3, 1});
    }

    if (ui->autologin_active) {
        char buf[64];
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed = now.tv_sec - ui->autologin_start.tv_sec;
        int remain = ui->autologin_delay - (int)elapsed;
        if (remain < 0) remain = 0;
        snprintf(buf, sizeof(buf), "Autologin in %d...", remain);
        draw_text_centered(r, ui->font, buf,
                           ui->screen_w / 2,
                           session_y + SESSION_HEIGHT + 36,
                           (struct slim_color){0.45, 0.45, 0.45, 1});
    }

    if (ui->dropdown_open) {
        int vis = ui->session_count < DD_MAX_VISIBLE
                ? ui->session_count : DD_MAX_VISIBLE;
        int dd_h = DD_PAD * 2 + vis * FIELD_HEIGHT + (vis - 1) * DD_GAP;
        int dd_y = session_y + SESSION_HEIGHT + 4;
        if (dd_y + dd_h > ui->screen_h - 10)
            dd_y = session_y - dd_h - 4;

        float dd_rad = (float)t->corner_radius;
        if (dd_rad > fw * 0.5f)
            dd_rad = fw * 0.5f;
        if (dd_rad > dd_h * 0.5f)
            dd_rad = dd_h * 0.5f;

        renderer_draw_rounded_rect(r, px + PANEL_PAD, dd_y, fw, dd_h,
                                   dd_rad,
                                   (struct slim_color){0.2, 0.2, 0.22, 0.98});

        float item_rad = (float)(FIELD_HEIGHT - 4) * 0.5f;
        if (item_rad > 12.0f)
            item_rad = 12.0f;

        for (int i = ui->dropdown_scroll;
             i < ui->session_count && i < ui->dropdown_scroll + DD_MAX_VISIBLE;
             i++) {
            int row = i - ui->dropdown_scroll;
            int item_y = dd_y + DD_PAD + row * (FIELD_HEIGHT + DD_GAP);

            if (i == ui->dropdown_hover) {
                renderer_draw_rounded_rect(r,
                    px + PANEL_PAD + DD_PAD, (float)item_y,
                    fw - 2 * DD_PAD, (float)FIELD_HEIGHT,
                    item_rad,
                    (struct slim_color){0.3, 0.34, 0.44, 1});
            }
            draw_text_centered(r, ui->font, ui->sessions[i].name,
                               px + pw / 2,
                               item_y + (FIELD_HEIGHT - font_h) / 2 + font_h,
                               i == ui->selected_session
                                   ? (struct slim_color){1,1,1,1}
                                   : (struct slim_color){0.7,0.7,0.7,1});
        }
    }

    {
        char footer[32];
        snprintf(footer, sizeof(footer), "SLiMM %s", SLIMM_VERSION);
        struct slim_color dim = {0.45f, 0.45f, 0.45f, 1.0f};
        int tw = font_text_width(ui->font, footer);
        draw_text(r, ui->font, footer,
                  ui->screen_w - tw - FOOTER_MARGIN,
                  ui->screen_h - FOOTER_MARGIN,
                  dim);
    }
}

enum ui_action ui_key(struct ui_state *ui, int sym, int pressed, const char *utf8)
{
    if (!pressed) return UI_NONE;
    ui->message[0] = '\0';

    if (ui->dropdown_open) {
        switch (sym) {
        case XKB_KEY_Up:
            if (ui->dropdown_hover > 0) {
                ui->dropdown_hover--;
                if (ui->dropdown_hover < ui->dropdown_scroll)
                    ui->dropdown_scroll--;
            }
            return UI_NONE;
        case XKB_KEY_Down:
            if (ui->dropdown_hover < ui->session_count - 1) {
                ui->dropdown_hover++;
                if (ui->dropdown_hover >= ui->dropdown_scroll + DD_MAX_VISIBLE)
                    ui->dropdown_scroll++;
            }
            return UI_NONE;
        case XKB_KEY_Return:
        case XKB_KEY_KP_Enter:
            ui->selected_session = ui->dropdown_hover;
            ui->dropdown_open = 0;
            return UI_NONE;
        case XKB_KEY_Tab:
        case XKB_KEY_Escape:
            ui->dropdown_open = 0;
            return UI_NONE;
        default:
            return UI_NONE;
        }
    }

    switch (sym) {
    case XKB_KEY_Tab:
        ui->autologin_active = 0;
        if (ui->session_count > 1) {
            ui->dropdown_open = 1;
            ui->dropdown_hover = ui->selected_session;
            ui->dropdown_scroll = 0;
        }
        return UI_NONE;

    case XKB_KEY_Escape:
        ui->autologin_active = 0;
        if (ui->input_mode == INPUT_PASSWORD && ui->password_len == 0) {
            ui->input_mode = INPUT_USERNAME;
            ui->field_dirty = 1;
            return UI_NONE;
        }
        if (ui->input_mode == INPUT_USERNAME && ui->username_len > 0) {
            memset(ui->username, 0, sizeof(ui->username));
            ui->username_len = 0;
            ui->field_dirty = 1;
            return UI_NONE;
        }
        return UI_QUIT;

    case XKB_KEY_F10:
        return UI_POWEROFF;
    case XKB_KEY_F11:
        return UI_REBOOT;
    case XKB_KEY_F12:
        return UI_SUSPEND;

    case XKB_KEY_Left:
        ui->autologin_active = 0;
        if (ui->session_count > 1)
            ui->selected_session = (ui->selected_session - 1 + ui->session_count) % ui->session_count;
        return UI_NONE;

    case XKB_KEY_Right:
        ui->autologin_active = 0;
        if (ui->session_count > 1)
            ui->selected_session = (ui->selected_session + 1) % ui->session_count;
        return UI_NONE;

    case XKB_KEY_Down:
        ui->autologin_active = 0;
        if (ui->session_count > 1) {
            ui->dropdown_open = 1;
            ui->dropdown_hover = ui->selected_session;
            ui->dropdown_scroll = 0;
        }
        return UI_NONE;

    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        ui->autologin_active = 0;
        if (ui->input_mode == INPUT_USERNAME) {
            if (ui->username_len > 0) {
                ui->input_mode = INPUT_PASSWORD;
                ui->field_dirty = 1;
            } else {
                snprintf(ui->message, sizeof(ui->message), "Enter username");
                ui->field_dirty = 1;
            }
            return UI_NONE;
        }
        if (ui->input_mode == INPUT_PASSWORD) {
            if (ui->password_len > 0 && ui->username_len > 0)
                return UI_LOGIN;
            snprintf(ui->message, sizeof(ui->message),
                     ui->password_len > 0 ? "Enter username" : "Enter password");
            ui->field_dirty = 1;
            return UI_NONE;
        }
        return UI_NONE;

    case XKB_KEY_BackSpace:
        ui->autologin_active = 0;
        if (ui->input_mode == INPUT_USERNAME && ui->username_len > 0) {
            ui->username[--ui->username_len] = '\0';
            ui->field_dirty = 1;
        } else if (ui->input_mode == INPUT_PASSWORD) {
            if (ui->password_len > 0) {
                ui->password[--ui->password_len] = '\0';
                ui->field_dirty = 1;
            } else {
                ui->input_mode = INPUT_USERNAME;
                ui->field_dirty = 1;
            }
        }
        return UI_NONE;

    default:;
        char c = (utf8 && utf8[0] && (unsigned char)utf8[0] >= 0x20 && !utf8[1]) ? utf8[0] : 0;
        if (c) {
            ui->autologin_active = 0;
            if (ui->input_mode == INPUT_USERNAME && ui->username_len < 63) {
                ui->username[ui->username_len++] = c;
                ui->username[ui->username_len] = '\0';
                ui->field_dirty = 1;
            } else if (ui->input_mode == INPUT_PASSWORD && ui->password_len < 63) {
                ui->password[ui->password_len++] = c;
                ui->password[ui->password_len] = '\0';
                ui->field_dirty = 1;
            }
        }
        return UI_NONE;
    }
}
