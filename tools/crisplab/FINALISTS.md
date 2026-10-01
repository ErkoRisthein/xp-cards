# Crispness finalists: parameters, C spec, cost

Blind sheets: `final/h{72,96,128,160,257}.png` (+ `_x2` for <= 160). The rows are XP, A, B, C, D.
The letter mapping is in `final/mapping.json` (seed 20261001):
**A = current, B = art+darkbias, C = art+darkbias+sharpen, D = art-only.**

## 1. Parameters (candidates/finalists.json)

| finalist | art (masters) | resampler |
|---|---|---|
| current | index stroke 115, courts as generated | box (exact area), as today |
| art-only | **index 130; court linework `#44F` → `#223`, width x1.6; court picture frame `#223`, 1.5 units** | box, as today (no code change) |
| art+darkbias | same new art | box in power space v^g, **g = 1 - 0.5 t** |
| art+darkbias+sharpen | same new art | box in v^g with **g = 1 - 0.4 t**, then a 3-tap sharpen **a = 0.2 t** (x then y) in the same space |

Ramp: `f = src_h / dst_h` (560/ch) and `t = clamp((f - 2) / 2, 0, 1)`. That gives full strength
for ch <= 140, t = 0.75 at h160, 0.40 at h200, 0.09 at h257, and 0 for ch >= 280. At t = 0 every
finalist is bit-identical to art-only.

## 2. Asset change (all three new finalists): tools/make_assets.sh `cards`
1. `INDEX_STROKE=${INDEX_STROKE:-130}` (was 115).
2. In J/Q/K only, every `<path>` or `<use>` with `stroke="#44F"` gets its `stroke-width` x1.6
   (the 3, 6, 36, 42.352, 48, 55.384, 65.454 and 72 widths in the 1300x2000 court art) and
   `stroke="#223"`. The court picture frame (`<use xlink:href="#X..." stroke="#44F" fill="none">`,
   with no width given, so 1 unit) gets `stroke="#223" stroke-width="1.5"`. `fill="#44F"` stays
   (the blue robes are unchanged).
   `proto/edit_svg.py <in> <out> 130 1.6 '#223' 1.5` does exactly this. Its output is
   pixel-identical to the lab's render, so it can replace the `sed` line:
   `"$PYTHON" tools/crisplab/proto/edit_svg.py "$f" "$TMP/$n.svg" "$INDEX_STROKE" 1.6 '#223' 1.5`
   (or inline it as a heredoc).
3. Re-run `tools/make_assets.sh cards check`. All 52 masters change because of the index. The
   size grows by about +7% (2.13 MB to 2.28 MB at the same PNG settings, so about 1.6 MiB with
   zopfli).
   The SVG sources stay unmodified. The checksums and docs/card-art.md §3 (index stroke, court
   frame colour) need updating.

## 3. Algorithm spec (C, integer/fixed point; reference: proto/resample_card.c)

New `FcImage *fc_image_resample_card(const FcImage *src, int w, int h, double g, double a)`,
called from `fc_cardset_card` in place of `fc_image_resample(m, cw, ch, quality)` when
quality = 1. The live-resize bilinear path (quality 0) and the kings stay as they are.

```
f = (double)src->h / h;  t = clamp((f - 2.0) / 2.0, 0, 1)
g_eff = 1 - (1 - G) * t        (G = 0.5 | 0.6)
a_eff = A * t                  (A = 0 | 0.2)
if (g_eff >= 1 && k1 == 0) return fc_image_resample(src, w, h, 1);   /* today's path, bit-identical */
```

1. **Precondition.** `src` is opaque: alpha 255 everywhere, which clean_master guarantees. Only
   B, G and R are processed, and the output alpha is 255. The card shape and frame still come
   from fc_image_card_finish afterwards. Do not use this path on anything with alpha < 255 (a
   power of a premultiplied value is not premultiplied).
2. **LUTs.** Rebuild these on every size change (4352 pow() calls; x87 is fine here and nowhere
   else):
   `fwd[i] = floor(65535 * pow(i/255.0, g_eff) + 0.5)` for i = 0..255 (uint16).
   `inv[j] = floor(255 * pow((16*j + 8)/65535.0, 1/g_eff) + 0.5)` for j = 0..4095 (uint8).
   The inverse lookup error is at most 0.12 LSB.
3. **Box resample in power space.** Use the unchanged `taps_build(…, quality 1)` exact-area
   weights (16.16, summing to exactly 65536).
   - Vertical pass, per output row: `acc[c] += fwd[src_c] * w` in uint32. This cannot overflow:
     the maximum is 65535·65536 < 2^32. Then `row16 = (acc + 32768) >> 16`.
   - Horizontal pass: `s = Σ row16 * w` in uint32 (same bound), then `v = (s + 32768) >> 16`,
     which lies in 0..65535. Store v in an int32 plane of w·h·3, or a uint16 plane when there is
     no sharpening.
4. **Sharpen (art+darkbias+sharpen only).** Weights in 4.12:
   `k1 = round(a_eff/4 * 4096)`, `k0 = 4096 + 2*k1`. Use clamp-to-edge neighbours.
   - Horizontal pass: `u[x] = (k0*v[x] - k1*(v[x-1] + v[x+1]) + 2048) >> 12`.
   - Then the same vertically on u, with no clamp between the passes. The int32 magnitudes stay
     below 2^30.
   - `>>` on negative int32 must be arithmetic. It is in GCC/MinGW; otherwise use floor division.
   This equals `(I + a(I - B))` per axis with `B = [1,2,1]/4`, i.e. the `sep` sharpen with
   sigma 0. Folding it into the taps instead is mathematically identical but costs more taps.
5. **Output.** `q = clamp(v, 0, 65535)`, then `c8 = inv[q >> 4]`. The pixel is
   `0xFF000000 | R<<16 | G<<8 | B`. Then call `fc_image_card_finish(...)` exactly as today.
6. **Memory.** acc and row take 3·src_w words. The plane takes 3·w·h int32 (586 KB at 190x257),
   plus one more of the same size when sharpening. All of it is freed per card.
7. **Optional.** Treat t < 0.1 as 0. At h257 (t = 0.09) the difference from art-only is at most
   5/255 on fewer than 5% of pixels, and invisible. This keeps 1080p maximised on today's code
   path.

Validation: `proto/check.py proto` shows the integer prototype within 1 LSB of the numpy model
for both finalists at h72-257.

## 4. Runtime cost (proto/resample_card.c --bench: 52 cards, best of 5, Apple M-series native, -O2)

| ch (cw) | box (today) | art+darkbias | ratio | art+darkbias+sharpen | ratio |
|---|---|---|---|---|---|
| 72 (53) | 9-11 ms | 16.1 ms | 1.45 | 14.5 ms | 1.57 |
| 96 (71) | 10.6 ms | 18.3 ms | 1.73 | 16.1 ms | 1.52 |
| 128 (95) | 11.5-12 ms | 17.4 ms | 1.52 | 18.2 ms | 1.51 |
| 160 (118) | 13.1 ms | 18.8 ms | 1.43 | 19.7 ms | 1.46 |
| 200 (148) | 14.9 ms | 22.0 ms | 1.47 | 23.7 ms | 1.59 |
| 257 (190) | 22-27 ms | 32.0 ms | 1.19 | 33.5 ms | 1.49 (both 1.0 with the t<0.1 cut) |
| 300 (222) | 25.9 ms | 25.7 ms | 1.0 | 27.4 ms | 1.0 |

- The extra cost is the fwd lookup per source sample (the master is 400x560 at every size), the
  16-bit accumulators, and the 6 MACs per output channel of the sharpen.
- On an XP-era single core (roughly 15-25x slower for this integer code), a full 52-card rebuild
  at h96 goes from about 0.2 s to 0.3 s. Sprites are built lazily per card, so the per-card
  first-draw cost goes from about 4-8 ms to 6-12 ms.
- art-only costs nothing extra.
- For comparison, the round-0 hypothesis (Lanczos-3 + light USM) needs 3.8-5.4x the taps of box
  (37 against 7 at h96, 15 against 4 at h257). Roughly 3-5x the time, and it was dominated on
  quality (rounds/r3_notes.md).

## 5. Metrics (out/finalists/metrics.csv)

| finalist | acut h96 | halo h96 | noise h96 | court contrast/ink h96 | acut h257 | halo h257 | court contrast/ink h257 |
|---|---|---|---|---|---|---|---|
| current | 44.5 | 0.14 | 1.03 | 68.7 / 7.5 | 20.2 | 0.01 | 76.4 / 7.5 |
| art-only | 46.1 | 0.19 | 1.04 | 70.9 / 13.3 | 20.8 | 0.03 | 82.0 / 23.1 |
| art+darkbias | 49.4 | 0.76 | 1.10 | 73.5 / 19.0 | 20.8 | 0.03 | 82.1 / 23.5 |
| art+darkbias+sharpen | 49.8 | 1.09 | 1.30 | 75.2 / 18.6 | 20.8 | 0.04 | 82.3 / 23.8 |
| XP original | 55.9 | - | - | 111.9 / 45.0 | - | - | - |

## 6. Shipped in v1.1.1: C + overshoot clamp

Both blind judges ranked C (art+darkbias+sharpen) first, ahead of B, art-only and current. The
aesthetics judge asked for one refinement, which ships as candidate `art+darkbias+sharpen+clamp`
(`candidates/shipped.json`):
- each sharpened sample (x pass, then y pass) is clamped to the [min, max] of the three samples it
  was computed from. This removes the stray near-white/cyan halo pixels in the court art. It also
  keeps every intermediate in 0..65535, so the C uses uint16 rows and a 3-row ring for the y pass
  instead of int32 planes;
- `t < 0.1` counts as 0 (`ramp_min` 0.1 in the lab): from h255 up (1080p maximised is h257) the
  v1.1 box code runs unchanged, bit for bit.

Art: exactly §2 (`tools/edit_card_svg.py`, called by `tools/make_assets.sh cards`). All 52
regenerated masters are pixel-identical to the lab's rasterisation of the C art. The 52 PNGs total
1,560,965 bytes (v1.1: 1,579,399).

C: `fc_image_resample_card` / `fc_card_filter_*` in `src/gfx/image.c`; the card set keeps one
`FcCardFilter` (LUTs and taps) per size. `proto/check.py shipped`: whole sprites within 1 LSB of the
model for AS 8H JD KH TC QH KS at h72-320. `tests/test_image.c` checks golden values from the model
(`proto/make_golden.py` → `tests/card_golden.h`) and a float reference of the spec. The Windows exe
(msvcrt `pow()` for the LUTs) renders the same pixels as the native build (e2e captures at
632x427 and 1904x996).

Metrics (6 cards, `crisplab.py metrics` / `artefacts`):

| h96 | acut | halo | halo_vis % | court noise | court contrast / ink |
|---|---|---|---|---|---|
| current (v1.1) | 44.5 | 0.14 | 0.38 | 1.03 | 68.7 / 7.4 |
| C | 49.8 | 1.09 | 3.86 | 1.30 | 75.2 / 18.6 |
| C + clamp (shipped) | 49.2 | 0.61 | 1.64 | 1.14 | 73.6 / 17.7 |

At h72 the clamp takes halo from 1.42 to 0.88 and noise from 1.34 to 1.18. At h160 it takes halo
from 0.49 to 0.19. Acutance drops by at most 0.7.

Cost: 52-card HQ rebuild through the card set (resample + finish), native -O2, Apple M-series, best
of 15. The flat-run shortcut in the sharpen (most of a card is white) pays for the clamp.

| ch | 72 | 96 | 128 | 160 | 200 | 257 |
|---|---|---|---|---|---|---|
| v1.1 (box) | 9.3 ms | 10.8 ms | 12.0 ms | 13.0 ms | 16.0 ms | 23.7 ms |
| v1.1.1 | 13.2 ms | 14.9 ms | 17.0 ms | 18.7 ms | 23.4 ms | 23.8 ms |
| ratio | 1.42 | 1.39 | 1.42 | 1.44 | 1.46 | 1.00 |
