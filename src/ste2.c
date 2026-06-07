#define _GNU_SOURCE
#include "ste2.h"
#include "theme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <GLES3/gl3.h>

static struct slim_color unpack(uint32_t p)
{
    return (struct slim_color){
        .r = ((p >> 24) & 0xFF) / 255.0f,
        .g = ((p >> 16) & 0xFF) / 255.0f,
        .b = ((p >> 8) & 0xFF) / 255.0f,
        .a = (p & 0xFF) / 255.0f,
    };
}

static uint32_t upload_tex(int w, int h, int fmt, const void *data)
{
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0,
                 fmt == GL_R8 ? GL_RED : GL_RGBA,
                 GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return tex;
}

int ste2_load(const char *path, struct slim_config *cfg,
              struct slim_images *img, struct slim_font **font,
              int screen_w, int screen_h)
{
    (void)screen_w;
    (void)screen_h;

    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "ste2: open '%s' failed\n", path);
        return -1;
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd);
        return -1;
    }

    size_t fsize = (size_t)st.st_size;

    void *map = mmap(NULL, fsize, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map == MAP_FAILED) {
        fprintf(stderr, "ste2: mmap failed\n");
        return -1;
    }

    struct ste2_header *hdr = (struct ste2_header *)map;
    if (memcmp(hdr->magic, STE2_MAGIC, 4) != 0 ||
        hdr->version != STE2_VERSION) {
        fprintf(stderr, "ste2: bad magic or version\n");
        munmap(map, fsize);
        return -1;
    }

    if (hdr->total_size > (uint32_t)fsize ||
        hdr->total_size < sizeof(struct ste2_header)) {
        fprintf(stderr, "ste2: invalid total_size\n");
        munmap(map, fsize);
        return -1;
    }

    if (hdr->atlas_w != GLYPH_ATLAS_SIZE || hdr->atlas_h != GLYPH_ATLAS_SIZE) {
        fprintf(stderr, "ste2: bad atlas dimensions\n");
        munmap(map, fsize);
        return -1;
    }

    {
        size_t end = (size_t)hdr->atlas_off + GLYPH_ATLAS_SIZE * GLYPH_ATLAS_SIZE;
        if (hdr->atlas_off < sizeof(struct ste2_header) || end > fsize) {
            fprintf(stderr, "ste2: bad atlas_off\n");
            munmap(map, fsize);
            return -1;
        }
    }

    if (hdr->glyph_count > MAX_GLYPHS) {
        fprintf(stderr, "ste2: too many glyphs %u\n", hdr->glyph_count);
        munmap(map, fsize);
        return -1;
    }

    {
        size_t gsize = (size_t)hdr->glyph_count * sizeof(struct ste2_glyph);
        size_t end = (size_t)hdr->glyph_off + gsize;
        if (hdr->glyph_off < sizeof(struct ste2_header) || end > fsize ||
            end > hdr->total_size) {
            fprintf(stderr, "ste2: bad glyph_off\n");
            munmap(map, fsize);
            return -1;
        }
    }

    if (hdr->bg_w && hdr->bg_h && hdr->bg_off) {
        size_t bsize = (size_t)hdr->bg_w * hdr->bg_h * 4;
        size_t end = (size_t)hdr->bg_off + bsize;
        if (end > fsize || end > hdr->total_size) {
            fprintf(stderr, "ste2: bad bg_off\n");
            munmap(map, fsize);
            return -1;
        }
    }

    if (hdr->logo_w && hdr->logo_h && hdr->logo_off) {
        size_t lsize = (size_t)hdr->logo_w * hdr->logo_h * 4;
        size_t end = (size_t)hdr->logo_off + lsize;
        if (end > fsize || end > hdr->total_size) {
            fprintf(stderr, "ste2: bad logo_off\n");
            munmap(map, fsize);
            return -1;
        }
    }

    memset(cfg, 0, sizeof(*cfg));
    cfg->theme.bg           = unpack(hdr->bg_color);
    cfg->theme.panel_bg     = unpack(hdr->panel_bg_color);
    cfg->theme.accent       = unpack(hdr->accent_color);
    cfg->theme.text         = unpack(hdr->text_color);
    cfg->theme.field_bg     = unpack(hdr->field_bg_color);
    cfg->theme.field_text   = unpack(hdr->field_text_color);
    cfg->theme.panel_width  = hdr->panel_width;
    cfg->theme.corner_radius = hdr->corner_radius;
    cfg->theme.font_size    = hdr->font_size;
    cfg->enable_numlock     = hdr->enable_numlock;
    cfg->autologin_delay    = hdr->autologin_delay;
    memcpy(cfg->autologin_user, hdr->autologin_user, sizeof(cfg->autologin_user));
    cfg->autologin_user[sizeof(cfg->autologin_user) - 1] = '\0';
    memcpy(cfg->default_session, hdr->default_session, sizeof(cfg->default_session));
    cfg->default_session[sizeof(cfg->default_session) - 1] = '\0';

    struct ste2_glyph *sg = (struct ste2_glyph *)((uint8_t *)map + hdr->glyph_off);

    struct slim_font *fnt = calloc(1, sizeof(struct slim_font));
    fnt->face = NULL;
    fnt->pixel_size = hdr->font_size;
    fnt->glyph_count = hdr->glyph_count;

    fnt->atlas_tex = upload_tex(hdr->atlas_w, hdr->atlas_h, GL_R8,
                                (uint8_t *)map + hdr->atlas_off);

    for (uint32_t i = 0; i < hdr->glyph_count; i++) {
        struct glyph_info *g = &fnt->glyphs[i];
        g->codepoint  = sg[i].codepoint;
        g->width      = sg[i].width;
        g->height     = sg[i].height;
        g->advance_x  = sg[i].advance_x;
        g->bearing_x  = sg[i].bearing_x;
        g->bearing_y  = sg[i].bearing_y;
        g->u0         = sg[i].u0;
        g->v0         = sg[i].v0;
        g->u1         = sg[i].u1;
        g->v1         = sg[i].v1;
    }

    *font = fnt;

    memset(img, 0, sizeof(*img));
    if (hdr->bg_w && hdr->bg_h && hdr->bg_off) {
        img->bg_w   = hdr->bg_w;
        img->bg_h   = hdr->bg_h;
        img->bg_tex = upload_tex(hdr->bg_w, hdr->bg_h, GL_RGBA,
                                 (uint8_t *)map + hdr->bg_off);
        img->has_bg = 1;
    }
    if (hdr->logo_w && hdr->logo_h && hdr->logo_off) {
        img->logo_w   = hdr->logo_w;
        img->logo_h   = hdr->logo_h;
        img->logo_tex = upload_tex(hdr->logo_w, hdr->logo_h, GL_RGBA,
                                   (uint8_t *)map + hdr->logo_off);
        img->has_logo = 1;
    }

    munmap(map, fsize);
    return 0;
}

void slim_images_free(struct slim_images *img)
{
    if (!img) return;
    if (img->bg_tex) glDeleteTextures(1, &img->bg_tex);
    if (img->logo_tex) glDeleteTextures(1, &img->logo_tex);
    memset(img, 0, sizeof(*img));
}
