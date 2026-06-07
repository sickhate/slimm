#define _GNU_SOURCE
#include "font.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GLES3/gl3.h>
#include <fontconfig/fontconfig.h>

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

static FT_Face open_face(FT_Library ft, const char *name)
{
    char *path = resolve_font_path(name);
    FT_Face face = NULL;

    if (path) {
        FT_New_Face(ft, path, 0, &face);
        if (face)
            fprintf(stderr, "font: loaded '%s' -> %s\n", name, path);
        free(path);
    }

    if (!face) {
        fprintf(stderr, "font: could not resolve '%s', trying fallbacks\n", name);
        static const char *fallbacks[] = {
            "/usr/share/fonts/TTF/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/noto/NotoSans-Regular.ttf",
            NULL,
        };
        for (int i = 0; fallbacks[i]; i++) {
            FT_New_Face(ft, fallbacks[i], 0, &face);
            if (face) break;
        }
    }

    return face;
}

static int ensure_glyph(struct slim_font *font, uint32_t cp)
{
    for (int i = 0; i < font->glyph_count; i++)
        if (font->glyphs[i].codepoint == cp)
            return i;

    if (font->glyph_count >= MAX_GLYPHS) return -1;

    if (FT_Load_Char(font->face, cp, FT_LOAD_RENDER)) {
        struct glyph_info *g = &font->glyphs[font->glyph_count++];
        g->codepoint = cp;
        g->width = 0; g->height = 0;
        g->advance_x = font->face->glyph->advance.x >> 6;
        g->bearing_x = 0; g->bearing_y = 0;
        g->u0 = g->v0 = g->u1 = g->v1 = 0;
        return font->glyph_count - 1;
    }

    FT_Bitmap *bm = &font->face->glyph->bitmap;

    if (font->atlas_x + bm->width + 1 > GLYPH_ATLAS_SIZE) {
        font->atlas_x = 0;
        font->atlas_y += font->atlas_row_h + 1;
        font->atlas_row_h = 0;
    }

    if ((int)bm->rows > font->atlas_row_h)
        font->atlas_row_h = bm->rows;

    if (font->atlas_y + font->atlas_row_h > GLYPH_ATLAS_SIZE) {
        fprintf(stderr, "font: glyph atlas full\n");
        return -1;
    }

    glBindTexture(GL_TEXTURE_2D, font->atlas_tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0,
                    font->atlas_x, font->atlas_y,
                    bm->width, bm->rows,
                    GL_RED, GL_UNSIGNED_BYTE, bm->buffer);

    struct glyph_info *g = &font->glyphs[font->glyph_count++];
    g->codepoint = cp;
    g->width = bm->width;
    g->height = bm->rows;
    g->advance_x = font->face->glyph->advance.x >> 6;
    g->bearing_x = font->face->glyph->bitmap_left;
    g->bearing_y = font->face->glyph->bitmap_top;
    g->u0 = (float)font->atlas_x / GLYPH_ATLAS_SIZE;
    g->v0 = (float)font->atlas_y / GLYPH_ATLAS_SIZE;
    g->u1 = (float)(font->atlas_x + bm->width) / GLYPH_ATLAS_SIZE;
    g->v1 = (float)(font->atlas_y + bm->rows) / GLYPH_ATLAS_SIZE;

    font->atlas_x += bm->width + 1;
    return font->glyph_count - 1;
}

struct slim_font *font_load(const char *name, int pixel_size)
{
    FT_Library ft;
    if (FT_Init_FreeType(&ft)) {
        fprintf(stderr, "font: FT_Init_FreeType failed\n");
        return NULL;
    }

    FT_Face face = open_face(ft, name);
    if (!face) {
        fprintf(stderr, "font: could not load '%s'\n", name);
        FT_Done_FreeType(ft);
        return NULL;
    }

    FT_Set_Pixel_Sizes(face, 0, pixel_size);

    struct slim_font *font = calloc(1, sizeof(struct slim_font));
    font->face = face;
    font->pixel_size = pixel_size;

    glGenTextures(1, &font->atlas_tex);
    glBindTexture(GL_TEXTURE_2D, font->atlas_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, GLYPH_ATLAS_SIZE, GLYPH_ATLAS_SIZE,
                 0, GL_RED, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    unsigned char *clear = calloc(1, GLYPH_ATLAS_SIZE * GLYPH_ATLAS_SIZE);
    if (clear) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                        GLYPH_ATLAS_SIZE, GLYPH_ATLAS_SIZE,
                        GL_RED, GL_UNSIGNED_BYTE, clear);
        free(clear);
    }

    font->atlas_x = 0;
    font->atlas_y = 0;
    font->atlas_row_h = 0;

    for (uint32_t cp = 0x20; cp < 0x7F; cp++)
        ensure_glyph(font, cp);

    FT_Done_FreeType(ft);
    return font;
}

void font_destroy(struct slim_font *font)
{
    if (!font) return;
    glDeleteTextures(1, &font->atlas_tex);
    if (font->face)
        FT_Done_Face(font->face);
    free(font);
}

struct glyph_info *font_get_glyph(struct slim_font *font, uint32_t codepoint)
{
    int idx = ensure_glyph(font, codepoint);
    if (idx < 0) return NULL;
    return &font->glyphs[idx];
}

int font_text_width(struct slim_font *font, const char *text)
{
    int w = 0;
    while (*text) {
        struct glyph_info *g = font_get_glyph(font, (unsigned char)*text);
        if (g) w += g->advance_x;
        text++;
    }
    return w;
}
