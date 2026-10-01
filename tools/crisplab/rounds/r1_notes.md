# Round 1: broad sweep (37 candidates; rounds/r1.json)

Outputs are in out/r1/: metrics.csv/.md, all/h*.png (every candidate), A..D/ (group sheets), and crops/.

Validation: the numpy port of clean_master, the box resample and card_finish matches the runtime C
(src/gfx/image.c + cardset.c, compiled natively in validate/harness.c) to within 1 LSB on a few
pixels. The rsvg rasterisation at 400x560 with stroke 115 is bit-identical to res/cards/*.png.

## Findings
- **The box filter is already "ideal".** `current` and `vector` (rsvg straight at the target size)
  agree to MAE 1.5/255 and have the same index acutance (44.5 vs 44.6 at h96). The softness is the
  sub-pixel art itself: index strokes are about 1 px at h96 and 0.8 px at h72, and court lines are
  0.1-0.2 px. No resampling kernel can recover that. Real gains have to come from sharpening past
  ideal AA, from darkening AA greys, or from bolder art.
- **Filters alone barely help.** Lanczos-3 gives +2% acutance at h96 (+4% at h72), with halo 0.6.
  Lanczos-4 gives no more acutance and more halo. Catmull-Rom and Lanczos-2 match box acutance
  with more halo. Triangle and Mitchell are softer than box. Linear-light is clearly worse
  (thinner, lighter strokes, MAE 5.7).
  PRUNED: tri, mitchell, catrom, lanczos2, lanczos4, box_linear.
- **Sharpening** (3-tap 'sep' folded into the kernel, or a 2D USM afterwards): acutance gain
  tracks halo almost linearly (about +2 acutance per +1 halo) for every filter+sharpen pair, so the
  choice is the amount, not the flavour. sep and usm2d look the same at equal halo, so sep wins
  because it costs nothing in C (the sharpening is folded into the separable taps). Above
  a≈0.6 (L3) or a≈1 (box), the court pictures turn crunchy and noisy at h96 (centre_h96_x3.png).
  At h257 and above, sharpening changes nothing visible (index_h257_x3.png).
  PRUNED: box+sep1, l3+sep.6, l3+usm1/.5 (too noisy on courts). Sharpening must ramp out for
  large cards.
- **New: dark-biased resampling (space_gamma g<1, i.e. averaging in v^g)** gives the best
  acutance-to-halo ratio: box sg.7 has acutance 46.7 with halo 0.38, and box sg.5 has 48.4 with
  halo 0.67, against box+sep.5 at 47.5 with halo 1.38. Flat colours are unchanged. AA'd dark
  strokes become more solid, like font "stem darkening". Pips and index look closest to XP's solid
  glyphs. tone_gamma (a post LUT) does the same to strokes but also shifts every flat colour
  (court chroma +15%); kept only as a washout option.
- **Courts:** #44F linework at any width stays pale blue (#44F 1.4x and #22B 1.6x leave ink
  almost unchanged). Dark navy #223 is the fix. 1.4-1.8x looks best at h72-128. 2.2x and #000
  1.6x turn faces into dark blobs at h96 and look heavy at h257. At h257 even 1.0-1.4x #223 is
  enough. A 1.5-unit frame in black or #223 gives XP's crisp picture frame.
  Court ink (% px with max channel < 110) at h96: current 7.5, #223 1.4x 10.8, 1.8x 16.6,
  2.2x 23.6 (XP 45).
  PRUNED: #44F at any width, #22B, 2.2x, #000 linework.
- **Index stroke:** 130 is clearly more legible at h72-96 and still elegant at h257 (closer to
  XP's bold index). 100 is worse. PRUNED: 100.

## Carried into round 2
Base filters box and lanczos3; space_gamma 0.6-0.8; sep sharpening 0.2-0.5 ramped by the
downscale factor; courts #223 1.4-1.8x, plus a size-adaptive variant and frame options; index
115/130/145.
