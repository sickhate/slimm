#ifndef SLIMM_THEME_H
#define SLIMM_THEME_H

#include <stdint.h>
#include "config.h"

struct slim_images {
    uint32_t bg_tex;
    uint32_t logo_tex;
    int bg_w, bg_h;
    int logo_w, logo_h;
    int has_bg;
    int has_logo;
};

void slim_images_free(struct slim_images *img);
void theme_load_images(struct slim_images *img, struct slim_theme *theme,
                       int screen_w, int screen_h);
void theme_free_images(struct slim_images *img);

#endif
