#define _GNU_SOURCE
#include "ste2.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <fontconfig/fontconfig.h>

#define STB_IMAGE_IMPLEMENTATION
#include "../third_party/stb_image.h"
#include "imgscale.h"

static int slimc_verbose(void)
{
    const char *v = getenv("SLIMC_VERBOSE");
    return v && v[0] && strcmp(v, "0") != 0;
}

#define slimc_vlog(...) do { if (slimc_verbose()) fprintf(stderr, __VA_ARGS__); } while (0)

static void slimc_logo_id(const char *logo_path, char *id, size_t id_sz)
{
    id[0] = '\0';
    if (!logo_path || !logo_path[0])
        return;
    const char *base = strrchr(logo_path, '/');
    base = base ? base + 1 : logo_path;
    size_t len = strlen(base);
    if (len > 4 && strcmp(base + len - 4, ".png") == 0)
        len -= 4;
    if (len >= id_sz)
        len = id_sz - 1;
    memcpy(id, base, len);
    id[len] = '\0';
}

static char *resolve_font_path(const char *name)
{
    if (name[0] == '/') return strdup(name);

    FcConfig *fc = FcInitLoadConfigAndFonts();
    if (!fc) return NULL;

    FcPattern *pat = FcNameParse((const FcChar8 *)name);
    FcConfigSubstitute(fc, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);

    FcResult res;
    FcPattern *match = FcFontMatch(fc, pat, &res);
    char *path = NULL;
    if (match) {
        FcChar8 *file = NULL;
        if (FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch)
            path = strdup((char *)file);
        FcPatternDestroy(match);
    }
    FcPatternDestroy(pat);
    FcConfigDestroy(fc);
    return path;
}

static int generate_atlas(const char *font_spec, int pixel_size,
                          uint8_t *atlas, struct ste2_glyph *glyphs)
{
    FT_Library ft;
    if (FT_Init_FreeType(&ft)) {
        fprintf(stderr, "slimc: FT_Init_FreeType failed\n");
        return -1;
    }

    char *fpath = resolve_font_path(font_spec);
    if (!fpath) {
        static const char *fallbacks[] = {
            "/usr/share/fonts/TTF/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/noto/NotoSans-Regular.ttf",
            NULL,
        };
        for (int i = 0; fallbacks[i]; i++) {
            fpath = strdup(fallbacks[i]);
            FT_Face test;
            if (FT_New_Face(ft, fpath, 0, &test) == 0) {
                FT_Done_Face(test);
                break;
            }
            free(fpath);
            fpath = NULL;
        }
        if (!fpath) {
            fprintf(stderr, "slimc: could not resolve font\n");
            FT_Done_FreeType(ft);
            return -1;
        }
    }

    FT_Face face;
    if (FT_New_Face(ft, fpath, 0, &face)) {
        fprintf(stderr, "slimc: FT_New_Face failed for '%s'\n", fpath);
        free(fpath);
        FT_Done_FreeType(ft);
        return -1;
    }
    slimc_vlog("slimc: loaded font '%s'\n", fpath);
    free(fpath);

    FT_Set_Pixel_Sizes(face, 0, pixel_size);
    memset(atlas, 0, GLYPH_ATLAS_SIZE * GLYPH_ATLAS_SIZE);

    int ax = 0, ay = 0, row_h = 0;
    int count = 0;

    for (uint32_t cp = 0x20; cp <= 0x7E; cp++) {
        struct ste2_glyph *g = &glyphs[count];
        memset(g, 0, sizeof(*g));
        g->codepoint = cp;

        if (FT_Load_Char(face, cp, FT_LOAD_RENDER)) {
            g->advance_x = face->glyph->advance.x >> 6;
            count++;
            continue;
        }

        FT_Bitmap *bm = &face->glyph->bitmap;

        if (ax + bm->width + 1 > GLYPH_ATLAS_SIZE) {
            ax = 0;
            ay += row_h + 1;
            row_h = 0;
        }
        if ((int)bm->rows > row_h)
            row_h = bm->rows;

        if (ay + row_h > GLYPH_ATLAS_SIZE) {
            fprintf(stderr, "slimc: glyph atlas full at U+%04X\n", cp);
            count++;
            continue;
        }

        for (int y = 0; y < (int)bm->rows; y++)
            memcpy(atlas + (ay + y) * GLYPH_ATLAS_SIZE + ax,
                   bm->buffer + y * bm->width, bm->width);

        g->width     = bm->width;
        g->height    = bm->rows;
        g->advance_x = face->glyph->advance.x >> 6;
        g->bearing_x = face->glyph->bitmap_left;
        g->bearing_y = face->glyph->bitmap_top;
        g->u0 = (float)ax / GLYPH_ATLAS_SIZE;
        g->v0 = (float)ay / GLYPH_ATLAS_SIZE;
        g->u1 = (float)(ax + bm->width) / GLYPH_ATLAS_SIZE;
        g->v1 = (float)(ay + bm->rows) / GLYPH_ATLAS_SIZE;

        ax += bm->width + 1;
        count++;
    }

    FT_Done_Face(face);
    FT_Done_FreeType(ft);
    return count;
}

static uint32_t pack_color(struct slim_color c)
{
    return ((uint8_t)(c.r * 255) << 24) |
           ((uint8_t)(c.g * 255) << 16) |
           ((uint8_t)(c.b * 255) << 8) |
           ((uint8_t)(c.a * 255));
}

int main(int argc, char *argv[])
{
    if (argc >= 2 && strcmp(argv[1], "--os-logo") == 0) {
        struct slim_theme t;
        theme_set_defaults(&t);
        if (!config_pick_logo(&t)) {
            fprintf(stderr, "slimc: no logo for this OS — add logos/<id>.png "
                            "(from /etc/os-release)\n");
            return 1;
        }
        printf("%s\n", t.logo_path);
        return 0;
    }

    if (argc >= 2 && strcmp(argv[1], "--os-logo-id") == 0) {
        char id[64];
        if (config_pick_logo_id(id, sizeof(id)) < 0) {
            fprintf(stderr, "slimc: no logo for this OS\n");
            return 1;
        }
        printf("%s\n", id);
        return 0;
    }

    if (argc < 3) {
        fprintf(stderr, "slimc — compile theme.toml into STE2 blob for production slimm\n\n");
        fprintf(stderr, "Usage: slimc theme.toml -o theme.slimt\n");
        fprintf(stderr, "       slimc --os-logo          print logo path for this OS\n");
        fprintf(stderr, "       slimc --os-logo-id       print distro id (e.g. arch)\n");
        fprintf(stderr, "\nSet SLIMC_VERBOSE=1 for full compile log.\n");
        return 1;
    }

    const char *toml_path = argv[1];
    const char *out_path  = NULL;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
            out_path = argv[i + 1];
    }
    if (!out_path) {
        fprintf(stderr, "slimc: missing -o output file\n");
        return 1;
    }

    struct slim_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    config_load(&cfg, toml_path);
    if (!config_pick_logo(&cfg.theme))
        fprintf(stderr, "slimc: warning: no logo for this OS (see logos/)\n");
    else
        slimc_vlog("slimc: using logo %s\n", cfg.theme.logo_path);

    struct ste2_header hdr;
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.magic, STE2_MAGIC, 4);
    hdr.version    = STE2_VERSION;
    hdr.atlas_w    = GLYPH_ATLAS_SIZE;
    hdr.atlas_h    = GLYPH_ATLAS_SIZE;

    hdr.bg_color        = pack_color(cfg.theme.bg);
    hdr.panel_bg_color  = pack_color(cfg.theme.panel_bg);
    hdr.accent_color    = pack_color(cfg.theme.accent);
    hdr.text_color      = pack_color(cfg.theme.text);
    hdr.field_bg_color  = pack_color(cfg.theme.field_bg);
    hdr.field_text_color= pack_color(cfg.theme.field_text);
    hdr.panel_width     = cfg.theme.panel_width;
    hdr.corner_radius   = cfg.theme.corner_radius;
    hdr.font_size       = cfg.theme.font_size;
    hdr.enable_numlock  = cfg.enable_numlock;
    hdr.autologin_delay = cfg.autologin_delay;
    memcpy(hdr.autologin_user, cfg.autologin_user, sizeof(hdr.autologin_user));
    memcpy(hdr.default_session, cfg.default_session, sizeof(hdr.default_session));

    uint8_t atlas[GLYPH_ATLAS_SIZE * GLYPH_ATLAS_SIZE];
    struct ste2_glyph glyphs[95];
    int glyph_count = generate_atlas(
        cfg.theme.font_path[0] ? cfg.theme.font_path : cfg.theme.font_name,
        cfg.theme.font_size, atlas, glyphs);
    if (glyph_count < 0) {
        fprintf(stderr, "slimc: font atlas generation failed\n");
        return 1;
    }
    hdr.glyph_count = glyph_count;

    uint8_t *bg_data = NULL;
    int bg_w = 0, bg_h = 0;
    if (cfg.theme.bg_image[0]) {
        int n, sw, sh;
        uint8_t *raw = stbi_load(cfg.theme.bg_image, &sw, &sh, &n, STBI_rgb_alpha);
        if (!raw) {
            fprintf(stderr, "slimc: failed to load bg '%s'\n", cfg.theme.bg_image);
        } else {
            int max_w = cfg.theme.bg_max_w > 0 ? cfg.theme.bg_max_w : STE2_BG_MAX_W;
            int max_h = cfg.theme.bg_max_h > 0 ? cfg.theme.bg_max_h : STE2_BG_MAX_H;
            int dw = sw, dh = sh;
            img_fit_dims(sw, sh, max_w, max_h, &dw, &dh);
            if (dw != sw || dh != sh) {
                bg_data = (uint8_t *)img_downscale_rgba(raw, sw, sh, dw, dh);
                stbi_image_free(raw);
                if (bg_data) {
                    bg_w = dw; bg_h = dh;
                    slimc_vlog("slimc: bg %dx%d -> %dx%d\n", sw, sh, dw, dh);
                }
            } else {
                bg_data = raw;
                bg_w = sw; bg_h = sh;
                slimc_vlog("slimc: bg %dx%d\n", bg_w, bg_h);
            }
        }
    }
    hdr.bg_w = bg_w;
    hdr.bg_h = bg_h;

    uint8_t *logo_data = NULL;
    int logo_w = 0, logo_h = 0;
    if (cfg.theme.logo_path[0]) {
        int n, sw, sh;
        uint8_t *raw = stbi_load(cfg.theme.logo_path, &sw, &sh, &n, STBI_rgb_alpha);
        if (!raw) {
            fprintf(stderr, "slimc: failed to load logo '%s'\n", cfg.theme.logo_path);
        } else {
            int dw = sw, dh = sh;
            img_fit_dims(sw, sh, STE2_LOGO_MAX, STE2_LOGO_MAX, &dw, &dh);
            if (dw != sw || dh != sh) {
                logo_data = (uint8_t *)img_downscale_rgba(raw, sw, sh, dw, dh);
                stbi_image_free(raw);
                if (logo_data) {
                    logo_w = dw; logo_h = dh;
                    slimc_vlog("slimc: logo %dx%d -> %dx%d\n", sw, sh, dw, dh);
                }
            } else {
                logo_data = raw;
                logo_w = sw; logo_h = sh;
                slimc_vlog("slimc: logo %dx%d\n", logo_w, logo_h);
            }
        }
    }
    hdr.logo_w = logo_w;
    hdr.logo_h = logo_h;

    uint32_t off = sizeof(hdr);
    hdr.atlas_off = off;
    off += GLYPH_ATLAS_SIZE * GLYPH_ATLAS_SIZE;

    hdr.glyph_off = off;
    off += glyph_count * sizeof(struct ste2_glyph);

    if (bg_data) {
        hdr.bg_off = off;
        off += bg_w * bg_h * 4;
    } else {
        hdr.bg_off = 0;
    }

    if (logo_data) {
        hdr.logo_off = off;
        off += logo_w * logo_h * 4;
    } else {
        hdr.logo_off = 0;
    }

    hdr.total_size = off;

    FILE *out = fopen(out_path, "wb");
    if (!out) {
        fprintf(stderr, "slimc: open '%s' for write failed\n", out_path);
        free(bg_data);
        free(logo_data);
        return 1;
    }

    fwrite(&hdr, sizeof(hdr), 1, out);
    fwrite(atlas, GLYPH_ATLAS_SIZE * GLYPH_ATLAS_SIZE, 1, out);
    fwrite(glyphs, sizeof(struct ste2_glyph), glyph_count, out);
    if (bg_data)
        fwrite(bg_data, bg_w * bg_h * 4, 1, out);
    if (logo_data)
        fwrite(logo_data, logo_w * logo_h * 4, 1, out);

    fclose(out);
    free(bg_data);
    free(logo_data);

    if (slimc_verbose()) {
        fprintf(stderr, "slimc: wrote %u bytes to '%s'\n", off, out_path);
    } else {
        char id[64];
        slimc_logo_id(cfg.theme.logo_path, id, sizeof(id));
        if (id[0])
            fprintf(stderr, "slimc: %s (logo %s, %u bytes)\n", out_path, id, off);
        else
            fprintf(stderr, "slimc: %s (%u bytes)\n", out_path, off);
    }
    return 0;
}
