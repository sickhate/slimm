#define _GNU_SOURCE
#include "drm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <gbm.h>
#include <drm_fourcc.h>
#include <errno.h>

#define MAX_DRM_DEVS 16

int drm_open(const char *path)
{
    int fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0) return -1;

    if (drmSetMaster(fd) < 0) {
        close(fd);
        return -1;
    }

    if (drmIsMaster(fd) == 0) {
        close(fd);
        return -1;
    }

    return fd;
}

int drm_find_device(void)
{
    static const char *paths[] = {
        "/dev/dri/card0", "/dev/dri/card1", "/dev/dri/card2",
        "/dev/dri/card3", "/dev/dri/card4",
    };

    for (int i = 0; i < 5; i++) {
        int fd = drm_open(paths[i]);
        if (fd >= 0) return fd;
    }

    return -1;
}

struct slimm_output *drm_find_output(int drm_fd)
{
    drmModeRes *res = drmModeGetResources(drm_fd);
    if (!res) {
        fprintf(stderr, "drm: failed to get resources\n");
        return NULL;
    }

    struct slimm_output *output = NULL;

    for (int i = 0; i < res->count_connectors && !output; i++) {
        drmModeConnector *conn = drmModeGetConnector(drm_fd, res->connectors[i]);
        if (!conn) continue;

        if (conn->connection == DRM_MODE_CONNECTED && conn->count_modes > 0) {
            drmModeEncoder *enc = NULL;
            uint32_t crtc_id = 0;
            int crtc_idx = -1;

            if (conn->encoder_id) {
                enc = drmModeGetEncoder(drm_fd, conn->encoder_id);
                if (enc && enc->crtc_id) {
                    crtc_id = enc->crtc_id;
                    for (int j = 0; j < res->count_crtcs; j++) {
                        if (res->crtcs[j] == crtc_id) {
                            crtc_idx = j;
                            break;
                        }
                    }
                }
            }

            if (!crtc_id) {
                for (int j = 0; j < res->count_encoders; j++) {
                    drmModeEncoder *e = drmModeGetEncoder(drm_fd, res->encoders[j]);
                    if (!e) continue;
                    for (int k = 0; k < res->count_crtcs; k++) {
                        if ((e->possible_crtcs & (1 << k)) && res->crtcs[k]) {
                            crtc_id = res->crtcs[k];
                            crtc_idx = k;
                            break;
                        }
                    }
                    drmModeFreeEncoder(e);
                    if (crtc_id) break;
                }
            }

            drmModeModeInfo *mode = &conn->modes[0];
            for (int m = 1; m < conn->count_modes; m++) {
                int cur = mode->hdisplay * mode->vdisplay;
                int cand = conn->modes[m].hdisplay * conn->modes[m].vdisplay;
                if (cand > cur || (cand == cur && conn->modes[m].vrefresh > mode->vrefresh))
                    mode = &conn->modes[m];
            }

            output = calloc(1, sizeof(struct slimm_output));
            output->drm_fd = drm_fd;
            output->connector_id = conn->connector_id;
            output->crtc_id = crtc_id;
            output->crtc_index = crtc_idx;
            output->width = mode->hdisplay;
            output->height = mode->vdisplay;
            output->format = DRM_FORMAT_XRGB8888;
            output->refresh_rate = mode->vrefresh;
            output->mode = *mode;

            if (enc) drmModeFreeEncoder(enc);
        }

        drmModeFreeConnector(conn);
    }

    drmModeFreeResources(res);
    return output;
}

static uint32_t get_fb_for_bo(int drm_fd, struct gbm_bo *bo, uint32_t format)
{
    uint32_t fb_id = (uintptr_t)gbm_bo_get_user_data(bo);
    if (fb_id) return fb_id;

    uint32_t stride = gbm_bo_get_stride(bo);
    uint32_t handle = gbm_bo_get_handle(bo).u32;

    uint32_t h[4] = {handle, 0, 0, 0};
    uint32_t s[4] = {stride, 0, 0, 0};
    uint32_t o[4] = {0};

    int ret = drmModeAddFB2(drm_fd, gbm_bo_get_width(bo), gbm_bo_get_height(bo),
                            format, h, s, o, &fb_id, 0);
    if (ret) {
        fprintf(stderr, "drm: drmModeAddFB2 failed: %d (%s)\n", ret, strerror(-ret));
        return 0;
    }

    gbm_bo_set_user_data(bo, (void *)(uintptr_t)fb_id, NULL);
    return fb_id;
}

struct slimm_gbm *drm_init_gbm(struct slimm_output *output)
{
    struct slimm_gbm *gbm = calloc(1, sizeof(struct slimm_gbm));
    gbm->drm_fd = output->drm_fd;
    gbm->output = output;

    gbm->dev = gbm_create_device(output->drm_fd);
    if (!gbm->dev) {
        fprintf(stderr, "drm: gbm_create_device failed\n");
        free(gbm);
        return NULL;
    }

    gbm->surface = gbm_surface_create(gbm->dev, output->width, output->height,
                                      output->format,
                                      GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    if (!gbm->surface) {
        fprintf(stderr, "drm: gbm_surface_create failed\n");
        gbm_device_destroy(gbm->dev);
        free(gbm);
        return NULL;
    }

    gbm->need_modeset = 1;
    return gbm;
}

void drm_flip(struct slimm_gbm *gbm, struct gbm_bo *bo)
{
    uint32_t fb_id = get_fb_for_bo(gbm->drm_fd, bo, gbm->output->format);
    if (!fb_id) return;

    int ret = drmModeSetCrtc(gbm->drm_fd, gbm->output->crtc_id, fb_id,
                             0, 0, &gbm->output->connector_id, 1,
                             &gbm->output->mode);
    if (ret) {
        fprintf(stderr, "drm: SetCrtc failed: %d (%s)\n", ret, strerror(-ret));
    }
}

void drm_handle_events(struct slimm_gbm *gbm)
{
    (void)gbm;
}

void drm_destroy(struct slimm_gbm *gbm)
{
    if (!gbm) return;
    if (gbm->surface) gbm_surface_destroy(gbm->surface);
    if (gbm->dev) gbm_device_destroy(gbm->dev);
    if (gbm->output) free(gbm->output);
    free(gbm);
}
