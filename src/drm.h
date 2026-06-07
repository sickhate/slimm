#ifndef SLIMM_DRM_H
#define SLIMM_DRM_H

#include <stdint.h>
#include <xf86drmMode.h>

struct slimm_output {
    int drm_fd;
    uint32_t connector_id;
    uint32_t crtc_id;
    uint32_t crtc_index;
    int width, height;
    uint32_t format;
    int refresh_rate;
    drmModeModeInfo mode;
};

struct slimm_gbm {
    int drm_fd;
    struct gbm_device *dev;
    struct gbm_surface *surface;
    struct slimm_output *output;
    struct gbm_bo *previous_bo;
    int flip_pending;
    int need_modeset;
};

int drm_open(const char *path);
int drm_find_device(void);
struct slimm_output *drm_find_output(int drm_fd);
struct slimm_gbm *drm_init_gbm(struct slimm_output *output);
void drm_flip(struct slimm_gbm *gbm, struct gbm_bo *bo);
void drm_handle_events(struct slimm_gbm *gbm);
void drm_destroy(struct slimm_gbm *gbm);

#endif
