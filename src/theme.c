#define _GNU_SOURCE
#include "theme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GLES3/gl3.h>

#define STB_IMAGE_IMPLEMENTATION
#include "../third_party/stb_image.h"
#include "imgscale.h"
#include "ste2.h"

static uint32_t load_texture(const char *path, int *out_w, int *out_h,
                             int max_w, int max_h)
{
    int n = 0;
    unsigned char *data = stbi_load(path, out_w, out_h, &n, 4);
    if (!data) {
        fprintf(stderr, "theme: could not load '%s': %s\n", path, stbi_failure_reason());
        return 0;
    }

    int w = *out_w, h = *out_h;

    int dw = w, dh = h;
    img_fit_dims(w, h, max_w, max_h, &dw, &dh);
    if (dw != w || dh != h) {
        unsigned char *scaled = img_downscale_rgba(data, w, h, dw, dh);
        stbi_image_free(data);
        if (!scaled) return 0;

        fprintf(stderr, "theme: downscaled '%s' %dx%d -> %dx%d\n", path, w, h, dw, dh);
        data = scaled;
        w = dw; h = dh;
        *out_w = w; *out_h = h;
    }

    uint32_t tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    stbi_image_free(data);
    return tex;
}

void theme_load_images(struct slim_images *img, struct slim_theme *theme,
                       int screen_w, int screen_h)
{
    memset(img, 0, sizeof(*img));

    if (theme->bg_image[0]) {
        int max_w = theme->bg_max_w > 0 ? theme->bg_max_w : STE2_BG_MAX_W;
        int max_h = theme->bg_max_h > 0 ? theme->bg_max_h : STE2_BG_MAX_H;
        if (screen_w > 0 && max_w > screen_w) max_w = screen_w;
        if (screen_h > 0 && max_h > screen_h) max_h = screen_h;
        img->bg_tex = load_texture(theme->bg_image, &img->bg_w, &img->bg_h,
                                   max_w, max_h);
        img->has_bg = (img->bg_tex != 0);
    }

    if (theme->logo_path[0]) {
        img->logo_tex = load_texture(theme->logo_path, &img->logo_w, &img->logo_h,
                                     STE2_LOGO_MAX, STE2_LOGO_MAX);
        img->has_logo = (img->logo_tex != 0);
    }
}

void theme_free_images(struct slim_images *img)
{
    slim_images_free(img);
}
