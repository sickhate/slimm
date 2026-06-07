#ifndef SLIMM_IMGSCALE_H
#define SLIMM_IMGSCALE_H

unsigned char *img_downscale_rgba(const unsigned char *src, int src_w, int src_h,
                                  int dst_w, int dst_h);
void img_fit_dims(int src_w, int src_h, int max_w, int max_h,
                  int *dst_w, int *dst_h);

#endif
