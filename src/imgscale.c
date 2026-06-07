/* Bilinear downscale — shared by slimc (compile-time) and theme.c (dev fallback) */
#include "imgscale.h"
#include <stdlib.h>

unsigned char *img_downscale_rgba(const unsigned char *src, int src_w, int src_h,
                                  int dst_w, int dst_h)
{
    if (!src || src_w < 1 || src_h < 1 || dst_w < 1 || dst_h < 1)
        return NULL;

    unsigned char *dst = malloc((size_t)dst_w * dst_h * 4);
    if (!dst) return NULL;

    for (int y = 0; y < dst_h; y++) {
        float sy = (y + 0.5f) * src_h / dst_h - 0.5f;
        int sy0 = sy < 0 ? 0 : (int)sy;
        int sy1 = sy0 + 1 < src_h ? sy0 + 1 : src_h - 1;
        float fy = sy - sy0;

        for (int x = 0; x < dst_w; x++) {
            float sx = (x + 0.5f) * src_w / dst_w - 0.5f;
            int sx0 = sx < 0 ? 0 : (int)sx;
            int sx1 = sx0 + 1 < src_w ? sx0 + 1 : src_w - 1;
            float fx = sx - sx0;

            for (int c = 0; c < 4; c++) {
                float v = (1 - fy) * ((1 - fx) * src[(sy0 * src_w + sx0) * 4 + c]
                                    + fx * src[(sy0 * src_w + sx1) * 4 + c])
                        + fy * ((1 - fx) * src[(sy1 * src_w + sx0) * 4 + c]
                              + fx * src[(sy1 * src_w + sx1) * 4 + c]);
                dst[(y * dst_w + x) * 4 + c] = (unsigned char)(v + 0.5f);
            }
        }
    }
    return dst;
}

void img_fit_dims(int src_w, int src_h, int max_w, int max_h,
                  int *dst_w, int *dst_h)
{
    *dst_w = src_w;
    *dst_h = src_h;
    if (max_w < 1 || max_h < 1) return;
    if (src_w <= max_w && src_h <= max_h) return;

    float scale = 1.0f;
    if (src_w > max_w) scale = (float)max_w / src_w;
    if (src_h * scale > max_h) scale = (float)max_h / src_h;

    *dst_w = (int)(src_w * scale);
    *dst_h = (int)(src_h * scale);
    if (*dst_w < 1) *dst_w = 1;
    if (*dst_h < 1) *dst_h = 1;
}
