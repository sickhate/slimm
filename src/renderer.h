#ifndef SLIMM_RENDERER_H
#define SLIMM_RENDERER_H

#include <stdint.h>
#include <gbm.h>
#include "config.h"

struct wl_display;
struct wl_egl_window;

struct slim_renderer;

struct slim_renderer *renderer_create(struct gbm_device *dev,
                                      struct gbm_surface *surface,
                                      int width, int height);
struct slim_renderer *renderer_create_wl(struct wl_display *wl_disp,
                                         struct wl_egl_window *win,
                                         int width, int height);
void renderer_destroy(struct slim_renderer *r);

void renderer_clear(struct slim_renderer *r, struct slim_color color);

void renderer_draw_rect(struct slim_renderer *r,
                        float x, float y, float w, float h,
                        struct slim_color color);

void renderer_draw_rounded_rect(struct slim_renderer *r,
                                float x, float y, float w, float h,
                                float radius, struct slim_color color);

void renderer_draw_texture(struct slim_renderer *r,
                           float x, float y, float w, float h,
                           uint32_t tex_id, struct slim_color tint);

/* Blit a render-target texture (top-down ortho FBO) right-side up */
void renderer_draw_texture_rt(struct slim_renderer *r,
                              float x, float y, float w, float h,
                              uint32_t tex_id, struct slim_color tint);

void renderer_draw_glyph(struct slim_renderer *r,
                         float x, float y, float w, float h,
                         uint32_t atlas_tex, float u0, float v0,
                         float u1, float v1, struct slim_color color);

int renderer_swap(struct slim_renderer *r);
struct gbm_bo *renderer_lock_front(struct slim_renderer *r);
void renderer_rebind(struct slim_renderer *r);

void renderer_set_viewport(struct slim_renderer *r, int width, int height);

#endif
