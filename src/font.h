#ifndef SLIMM_FONT_H
#define SLIMM_FONT_H

#include <stdint.h>
#ifndef SLIMM_MINIMAL
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

#define GLYPH_ATLAS_SIZE 512
#define MAX_GLYPHS 256

struct glyph_info {
    uint32_t codepoint;
    int width, height;
    int advance_x;
    int bearing_x, bearing_y;
    float u0, v0, u1, v1;
};

struct slim_font {
#ifndef SLIMM_MINIMAL
    FT_Face face;
#else
    void *face;
#endif
    uint32_t atlas_tex;
    struct glyph_info glyphs[MAX_GLYPHS];
    int glyph_count;
    int atlas_x, atlas_y, atlas_row_h;
    int pixel_size;
};

#ifndef SLIMM_MINIMAL
struct slim_font *font_load(const char *name, int pixel_size);
#endif
void font_destroy(struct slim_font *font);
struct glyph_info *font_get_glyph(struct slim_font *font, uint32_t codepoint);
int font_text_width(struct slim_font *font, const char *text);

#endif
