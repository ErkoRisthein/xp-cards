# crisplab: card crispness test bench

`crisplab.py` reproduces the runtime card pipeline (src/gfx/cardset.c + src/gfx/image.c) in numpy
for any number of **candidates** (JSON dicts of parameters). It renders comparison sheets,
zoomed crops, metrics, artefact checks and blind A/B sheets, so changes to the art or the
resampler can be judged at every display size before any C is written.

The bench is faithful. `proto/check.py runtime` renders the shipped masters with the unchanged
runtime box code and agrees with the model to within 1 LSB at h72-320; `proto/check.py shipped`
does the same for the shipped card resampler (`fc_image_resample_card`, v1.1.1) against candidate
`art+darkbias+sharpen+clamp`. That candidate's rsvg rasterisation is bit-identical to
`res/cards/*.png` (v1.1.1); the default candidate's was bit-identical to the v1.1 masters.

**Shipped (v1.1.1):** `candidates/shipped.json` → `art+darkbias+sharpen+clamp` (the finalist C of
FINALISTS.md plus the overshoot clamp and the t < 0.1 cut). The lab defaults (`{"name": "current"}`)
still describe v1.1, so the round files keep their meaning.

Needs Python 3 with numpy and Pillow, plus `rsvg-convert` on PATH. The XP reference row needs
the cards.dll bitmaps `cards_bitmap_<id>.png` (71x96, id = suit*13 + rank + 1, suits C D H S,
ranks A..K).

```sh
export CRISPLAB_XP=/path/to/xp/bitmaps      # optional: adds the 'XP original' row (nearest-scaled)
export CRISPLAB_CACHE=/path/to/cache        # rsvg raster cache (default /tmp/crisplab-cache)
L=tools/crisplab/crisplab.py; C=tools/crisplab/candidates/finalists.json

python3 $L all       -c $C -o out/fin                     # sheets h72..h300 (+ _x2 <= 160) + metrics.csv/.md
python3 $L sheets    -c $C -o out/s --heights 96,257 --cards AS,KH --no-column --only current,art-only
python3 $L crops -t  -c $C -o out/c --heights 72,96 --cards TC,8H,KH,col --region index --zoom 4
python3 $L artefacts -c $C -o out/a --heights 72,96,257  # shape/alpha, red-pip hue & fringe, court noise
python3 $L blind     -c $C -o final --heights 72,96,128,160,257 --seed 1   # rows A-D + XP, mapping.json
python3 $L card      -c $C -o out/one --heights 96 --cards KH            # single sprites on the table
```

## Candidate parameters (defaults reproduce today's pipeline: `{"name": "current"}`)

| key | default | meaning |
|---|---|---|
| index_stroke | 115 | rank-index stroke width (generator 80; make_assets.sh `$INDEX_STROKE`) |
| court_mult / court_colour | 1.0 / keep `#44F` | court-art linework (`stroke="#44F"` path/use inside J/Q/K art): width multiplier and colour |
| court_frame_w / court_frame_colour | 1.0 / keep | the rectangle around the court picture |
| court_fill, colour_map | none | recolour `#44F` fills; arbitrary `{"#FC4": "#FB2"}` remaps in courts |
| source, master_h, master_w | 'master', 560, 400 | `'vector'` rasterises straight at cw x ch, the ideal-AA reference |
| filter, blur | 'box', 1 | box (exact area, as image.c), triangle, hermite, mitchell, catrom, lanczos2/3/4, magic; separable, x/y factors independent, kernel stretched by the factor, edge window renormalised |
| sharpen | none | `{"mode": "sep"\|"usm2d", "sigma": s, "amount": a, "ramp": [f0, f1]}`. sep is `(1+a)δ - a·G_s` at output scale, folded into the separable kernel (sigma 0 = 3-tap `[-a/4, 1+a/2, -a/4]`). usm2d is a 2D unsharp mask after 8-bit rounding. `"clamp": true` (sep, sigma 0): x then y passes after the box, each sample clamped to the [min, max] of its three taps (no overshoot), as image.c |
| ramp_min | 0 | ramp values t below this count as 0 (the runtime's `t < 0.1` cut: h257 is the plain box) |
| space_gamma | 1 | resample `v^g` and return with `^(1/g)`. g < 1 averages with a dark bias ("stem darkening"); flat colours are unchanged |
| tone_gamma | 1 | post-resample per-channel LUT `255(v/255)^g` |
| tone_ramp | none | `[f0, f1]`: space/tone gamma ramps from 1 at downscale factor f0 to g at f1 |
| linear | false | resample in linear light |
| by_height | none | `[[max_h, {overrides}], ...]` for size-adaptive art/filters |

The ramps use f = master_h / ch: 5.83 at h96, 2.18 at h257. `t = clamp((f-f0)/(f1-f0), 0, 1)`.

Cards: AS TC 8H JD QS KH, plus a FreeCell column KS QH JC TD 9S at XP's step 9·ch/46, which
shows the buried index strips. Sizes: cw = round(71·ch/96).

## Metrics (metrics.csv; averaged over the six cards)
- `acut`: mean |∇luma| on the top-left index (rank + suit). Higher is crisper. `acut_ref` is the
  same for the vector-at-target render of the same art.
- `halo` / `halo_vis`: overshoot per channel beyond the 3x3 local min/max of the vector
  reference, on pixels within 2 px of an edge. `halo` is the mean in 0-255 units; `halo_vis` is
  the % of samples over 12.
- `mae`: mean abs difference from the vector reference of the same art. It measures departure
  from ideal AA, not quality. Box ≈ vector, so any sharpening or emboldening raises it.
- Court picture (J/Q/K): `contrast` (luma std), `ink` (% px with max(R,G,B) < 110, i.e. dark
  linework), `chroma` (mean max-min), `lum`. For "washout", contrast and ink are the useful
  ones. Chroma drops with dark-navy linework, even though the court reads far less washed out.
- `artefacts`: alpha_diff/outside_diff (the card shape and the table must not change),
  red_GB (hue shift on 8H), red_fringe (dark pixels around red pips), and court_noise
  (|Laplacian| in the court relative to ideal AA, where 1.0 = as busy as the vector render).

## Files
- `candidates/finalists.json`: the four finalists. `candidates/shipped.json`: v1.1 (`current`),
  finalist C and the shipped `art+darkbias+sharpen+clamp`. `rounds/r{1,2,3}.json` + `*_notes.md`
  hold the sweep history (what was tried and why it was pruned).
- `FINALISTS.md`: the finalists' parameters, the C algorithm spec, cost, and the asset changes;
  §6 is what shipped in v1.1.1.
- `proto/resample_card.c`: integer C prototype of finalists B/C (+ `--bench`).
  `proto/runtime_card.c` renders one sprite with the runtime code (`box`: v1.1's resampler,
  `card`: the shipped `fc_image_resample_card`).
  `proto/check.py` cross-checks them against the model (`runtime`, `shipped`, `proto`). `proto/edit_svg.py` is the art edit
  prototype (shipped as `tools/edit_card_svg.py`). `proto/make_golden.py` writes
  `tests/card_golden.h` (the model's values for tests/test_image.c); re-run it after changing the
  masters or the resampler:
  `python3 tools/crisplab/proto/make_golden.py`.
