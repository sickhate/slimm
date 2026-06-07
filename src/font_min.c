/* STE2-only glyph lookup — no FreeType/fontconfig */
#include "font.h"
#include <stdlib.h>
#include <GLES3/gl3.h>

struct glyph_info *font_get_glyph(struct slim_font *font, uint32_t codepoint)
{
    for (int i = 0; i < font->glyph_count; i++) {
        if (font->glyphs[i].codepoint == codepoint)
            return &font->glyphs[i];
    }
    return NULL;
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

void font_destroy(struct slim_font *font)
{
    if (!font) return;
    glDeleteTextures(1, &font->atlas_tex);
    free(font);
}
