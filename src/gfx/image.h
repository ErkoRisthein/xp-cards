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

/* The card-master resampler (docs/DESIGN.md "Crispness decisions"). For strong downscales it keeps
 * thin strokes dark and edges crisp: with t = clamp((src_h / h - 2) / 2, 0, 1) (full strength for
 * h <= src_h / 4, i.e. ch <= 140 from the 560-px masters), it box-averages v^g with g = 1 - 0.4 t (a
 * dark bias, "stem darkening"), applies a 3-tap sharpen [-a/4, 1 + a/2, -a/4] with a = 0.2 t along x
 * then y, each sample clamped to the range of its three inputs (no halos), and maps back with
 * v^(1/g). A flat colour stays exactly the same. For t < 0.1 (h >= 255 from 560) the result is
 * exactly fc_image_resample(src, w, h, 1).
 * src must be opaque (alpha 255 everywhere, as the card set's cleaned masters are); a source that is
 * not is resampled with fc_image_resample(src, w, h, 1) instead. The output is opaque.
 * The per-size part (LUTs from pow(), filter taps) is an FcCardFilter, built once per size; the
 * per-card part is integer only. */
typedef struct FcCardFilter FcCardFilter;
FcCardFilter *fc_card_filter_new(int src_w, int src_h, int w, int h);   /* NULL if out of memory */
void          fc_card_filter_free(FcCardFilter *f);
int           fc_card_filter_is(const FcCardFilter *f, int src_w, int src_h, int w, int h);
FcImage      *fc_image_resample_card_filter(const FcImage *src, const FcCardFilter *f); /* src: f's src size */
/* One-off: quality 1 builds a filter for this call; quality 0 is fc_image_resample(src, w, h, 0). */
FcImage      *fc_image_resample_card(const FcImage *src, int w, int h, int quality);

/* Drawing into dst (all clip to dst bounds). */
void fc_fill_rect(FcImage *dst, int x, int y, int w, int h, uint32_t argb);       /* opaque copy */
void fc_blit(FcImage *dst, const FcImage *src, int dx, int dy);                   /* src-over */
void fc_blit_inverted(FcImage *dst, const FcImage *src, int dx, int dy);          /* src-over of (a - c) per channel */
void fc_copy_rect(FcImage *dst, int dx, int dy, const FcImage *src, int sx, int sy, int w, int h); /* raw copy */
/* Invert dst (opaque) where mask covers it: each colour channel c becomes 255 - c, blended by the
 * mask's alpha (a card sprite: the card's rounded shape). */
void fc_invert_masked(FcImage *dst, const FcImage *mask, int dx, int dy);
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

/* The same finish with the shape precomputed: the per-pixel coverage (floating point, slow on x87)
 * depends only on (w, h, r, t), so a card set computes it once per card size and applies it to every
 * card with integer math. The result is identical to fc_image_card_finish. */
typedef struct FcCardShape FcCardShape;
FcCardShape *fc_card_shape_new(int w, int h, double r, double t);   /* NULL if out of memory */
void         fc_card_shape_free(FcCardShape *shape);
int          fc_card_shape_is(const FcCardShape *shape, int w, int h, double r, double t);
void         fc_image_card_finish_shape(FcImage *img, const FcCardShape *shape, uint32_t frame,
                                        uint32_t under);   /* img must be shape's size */

/* An empty-cell / king-frame bevel for scaled-up boards as a w x h premultiplied image: an
 * anti-aliased ring of thickness t with rounded corners (outer radius rad), colour tl on the top/left
 * edges and br on the bottom/right, mitred at 45 degrees through the top-right and bottom-left
 * corners. Pixels outside the ring are 0 (untouched when drawn). NULL if out of memory. */
FcImage *fc_bevel_ring_new(int w, int h, double t, double rad, uint32_t tl, uint32_t br);

/* Write a PNG (un-premultiplied) — available in native builds/tests only (FC_WITH_PNG_WRITER).
 * Without FC_WITH_PNG_WRITER it is a stub returning 0. */
int fc_image_write_png(const FcImage *img, const char *path);

#endif
