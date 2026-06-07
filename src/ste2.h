#ifndef SLIMM_STE2_H
#define SLIMM_STE2_H

#include <stdint.h>
#include "config.h"
#include "theme.h"
#include "font.h"

#define STE2_MAGIC   "SLMT"
#define STE2_VERSION 1
#define GLYPH_ATLAS_SIZE 512

/* Compile-time caps — keeps GPU + mmap footprint low */
#define STE2_BG_MAX_W  1280
#define STE2_BG_MAX_H  720
#define STE2_LOGO_MAX    256

struct ste2_glyph {
    uint32_t codepoint;
    int16_t  width, height;
    int16_t  advance_x;
    int16_t  bearing_x, bearing_y;
    float    u0, v0, u1, v1;
} __attribute__((packed));

struct ste2_header {
    char     magic[4];
    uint32_t version;
    uint32_t total_size;

    uint32_t atlas_w, atlas_h;
    uint32_t bg_w, bg_h;
    uint32_t logo_w, logo_h;

    uint32_t bg_color;
    uint32_t panel_bg_color;
    uint32_t accent_color;
    uint32_t text_color;
    uint32_t field_bg_color;
    uint32_t field_text_color;

    uint16_t panel_width;
    uint16_t corner_radius;
    uint16_t font_size;
    uint16_t enable_numlock;

    char     autologin_user[32];
    int32_t  autologin_delay;
    char     default_session[64];

    uint32_t atlas_off;
    uint32_t glyph_off;
    uint32_t bg_off;
    uint32_t logo_off;
    uint32_t glyph_count;

    uint8_t  _pad[4];
} __attribute__((packed));

_Static_assert(sizeof(struct ste2_header) == 192, "ste2_header must be 192 bytes");

int ste2_load(const char *path, struct slim_config *cfg,
              struct slim_images *img, struct slim_font **font,
              int screen_w, int screen_h);
void ste2_free(void *map, size_t len);

#endif
