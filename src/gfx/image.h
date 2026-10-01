/*
 * FreeCell HD — software images (platform independent).
 *
 * Pixels are 32-bit premultiplied BGRA stored as uint32_t 0xAARRGGBB (little-endian memory order
 * B,G,R,A — identical to a Win32 32-bpp top-down DIB section, so a framebuffer can wrap DIB bits).
 */
#ifndef FC_IMAGE_H
#define FC_IMAGE_H

#include <stddef.h>
#include <stdint.h>

typedef struct FcImage {
    int       w, h;
    int       stride;   /* in pixels */
    uint32_t *px;
    int       owns;     /* 1 if px is freed by fc_image_free */
} FcImage;

#define FC_ARGB(a, r, g, b) (((uint32_t)(a) << 24) | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#define FC_RGB(r, g, b)     FC_ARGB(255, r, g, b)

FcImage *fc_image_new(int w, int h);                               /* zero (transparent) pixels */
FcImage  fc_image_wrap(int w, int h, int stride, uint32_t *px);     /* non-owning view */
void     fc_image_free(FcImage *img);

/* Decode a PNG from memory into a premultiplied image (NULL on failure). */
FcImage *fc_image_decode_png(const void *data, size_t len);

/* High-quality resample to w x h (independent x/y scale). Downscaling uses area averaging (box filter
 * over exact pixel footprints) in premultiplied space; upscaling uses bilinear. quality 0 = fast
 * (bilinear / nearest mip, for live window resizing), 1 = best. */
FcImage *fc_image_resample(const FcImage *src, int w, int h, int quality);

/* Drawing into dst (all clip to dst bounds). */
void fc_fill_rect(FcImage *dst, int x, int y, int w, int h, uint32_t argb);       /* opaque copy */
void fc_blit(FcImage *dst, const FcImage *src, int dx, int dy);                   /* src-over */
void fc_blit_inverted(FcImage *dst, const FcImage *src, int dx, int dy);          /* src-over of (a - c) per channel */
void fc_copy_rect(FcImage *dst, int dx, int dy, const FcImage *src, int sx, int sy, int w, int h); /* raw copy */
/* Anti-aliased rounded-rectangle outline of thickness t (pixels, may be fractional), drawn inside the
 * rect (x,y,w,h) with outer corner radius r, src-over with colour argb (straight, will be premultiplied). */
void fc_stroke_round_rect(FcImage *dst, double x, double y, double w, double h, double r, double t, uint32_t argb);

/* Area of pixel (px,py)'s unit square inside the rounded rect (x,y,w,h, corner radius r), 0..1. */
double fc_round_rect_coverage(double x, double y, double w, double h, double r, int px, int py);

/* Finish a scaled card sprite in place: the art is composited over 'under' (straight ARGB) inside an
 * analytic rounded-rect shape of the image's full size (outer radius r), and a frame ring of
 * thickness t in colour 'frame' (straight ARGB) is drawn along its edge. Outside the shape the
 * sprite is transparent, so corners stay clean (no dark fringes) at every size. */
void fc_image_card_finish(FcImage *img, double r, double t, uint32_t frame, uint32_t under);

/* Write a PNG (un-premultiplied) — available in native builds/tests only (FC_WITH_PNG_WRITER).
 * Without FC_WITH_PNG_WRITER it is a stub returning 0. */
int fc_image_write_png(const FcImage *img, const char *path);

#endif
