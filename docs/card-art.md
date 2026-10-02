# High-resolution card art: sourcing and preparation

Root: `(scratch)/`
(scratch work and candidate renders are in `../research/art/`)

## 1. Choice: Adrian Kennard (RevK) "SVG playing cards", CC0 1.0

- **Licence:** CC0 1.0, stated by the author on the primary source page https://www.me.uk/cards/:
  "offer my SVG playing card sets … to the public domain", "You can do what you like with these
  designs", "© Copyright 2018 Adrian Kennard Released under CC0 Public Domain licence."
  The court cards are "based on 19th Century Goodall & Son designs". The full evidence, the page
  snapshot, the Wayback URL, the CC0 legal code and a GPL caveat about the generator program are in
  `LICENSE-ART.md` and `src/`.
- **Generator options used:** Poker size, Ace of Spades "Fancy" with no text and no QR code,
  **Index size = Medium** (`super=1`), Normal pips, **Wider court cards** (`wider=on`), red `#ff0000`,
  black `#000000` and a Diamond back. The exact URL is in `src/GENERATOR_URL.txt`, and the raw zip
  is in `src/revk_generator_output.zip`.
- **SVGs:** `svg/<R><S>.svg` (52 files, 577,516 bytes in total) plus `svg/back_diamond_{black,red}.svg`.
  Each file is self-contained: no `<text>`, no `<image>`, no external references and no fonts.
  Indices are stroked paths, so they render identically everywhere.

### Why this set (candidates compared side by side at 96, 150, 280 and 420 px)
Comparison images: `../research/art/cmp96_2x.png` (rows: Fomin, Knoll, RevK normal index, RevK medium
index, XP original), `cmp150.png`, `courts420.png`, `fomin_quick.png`, `knoll_quick.png`,
`revk_quick.png`, `revk_variants.png`, `xp_grid2x.png`.

| Criterion | XP cards.dll (reference) | **RevK (chosen)** | Byron Knoll 1.3 (runner-up) | Dmitry Fomin (Commons) |
|---|---|---|---|---|
| Licence | n/a | CC0 | Public domain | CC0 |
| Aspect W/H | 71/96 = 0.740 | 240/336 = **0.714** (2.5×3.5 in) | 0.689 | 360/540 = 0.667 |
| Red | `#FF0000` | **`#FF0000`** | `#DF0000` plus gradients | `#FF5555` (salmon) |
| Index rank height / card height | 12/96 = 12.5 % (bold, 2 px strokes) | 83/560 = **14.8 %** | ~10 % | ~9 %, thin and faint at 96 px |
| A♥ A♦ A♣ | small single pip | **small single pip** | giant glossy gradient pip | small pip |
| A♠ | large ornate spade | large spade with decorative outline | **ornate spade (closest)** | small plain pip |
| Pip size on 2–10 | moderate | **moderate** | oversized and crowded (10♣ pips touch the index) | moderate |
| Courts | framed, saturated red, yellow and black | framed (thin `#44F` line), Goodall pattern, slightly pastel at small sizes | framed (black line), **most saturated** | unframed, salmon red |
| Corner radius / width | ~2 px / 71 | 5.2 % | 4.1 % | 8.3 % (very round) |

RevK matches the XP look best on 40 of 52 cards: the pip cards and aces, the index size and
placement, pure red, and the aspect closest to XP. It is cleanly built, generated from code and
fully consistent, and it has the strongest licence statement. Its only weakness is that the courts
look a little paler than XP's at about 96 px. Knoll's courts are punchier, but its pip cards and aces
look nothing like XP. Fomin's courts are the same drawings as Knoll's in different colours, and its
small faint indices and salmon red ruled it out. A hybrid (Knoll courts with RevK pips) is possible
but would mix border and index styles, so I did not do it.

Index size: "Normal" (`super=0`) gives an 8.8 % rank glyph that is too thin and small at 96 px.
"Large" (`super=2`) gives about 20 %, which is too big. "Medium" is closest to XP. "Wider" widens
the court frame, which Medium otherwise narrows (compared in `revk_variants.png`).

## 2. Master PNGs: `master/<rank><suit>.png`

- Ranks are `A 2 3 4 5 6 7 8 9 T J Q K` and suits are `C D H S`. There are 52 files, all present.
- **Source aspect:** 5:7, from viewBox `-120 -168 240 336`. **Master size: 400×560 px** exactly
  (height 560, width 560×240/336 = 400). The scale is **1 SVG unit = 1.6667 px**.
- Rasterized with `rsvg-convert -h 560` (librsvg 2.63.2, cairo 1.18.4).
- **Art edit before rasterising (v1.1.1, `tools/edit_card_svg.py`, run by `tools/make_assets.sh
  cards`; the SVGs in the repo stay as generated):** the rank-index stroke goes from 80 to **130**
  units, and in J/Q/K the court-art linework (`stroke="#44F"`, widths 3 to 72 in the 1300x2000 court
  art) becomes **1.6x wider and `#223`** (dark navy), the court picture frame **`#223` at 1.5 units**.
  The blue fills stay. Why: docs/DESIGN.md "Crispness decisions" (the original thin light-blue lines
  wash the courts out to pastel at every size; the thin index fades at small sizes).
- Format: PNG, 8-bit RGBA (colour type 6) in every file, straight (non-premultiplied) alpha. Pixels
  outside the rounded corners are fully transparent; I checked the four corner pixels (alpha 0) and
  the centre (alpha 255) of every file.
- The card fills the whole canvas, with no margin: the outer edge of the outline is at pixel 0 and
  pixel 399/559.
- Optional extras: `extra/back_diamond_black.png` and `extra/back_diamond_red.png` (400×560). Their
  fine diamond pattern moirés when scaled down. FreeCell does not need a back.

### Size on disk (52 masters)
| Method | Bytes |
|---|---|
| rsvg-convert raw output | 2,245,827 |
| `magick -strip -define png:compression-level=9 -define png:compression-filter=5 PNG32:` | 2,166,051 |
| oxipng (pyoxipng 9.x), level 6, libdeflate 12, RGBA forced | 1,623,601 |
| **oxipng, zopfli 15 iterations, RGBA forced (the files shipped in `master/`)** | **1,587,142 (1.51 MiB)** |
| oxipng with colour-type reduction allowed (20 black cards become grey+alpha) | 1,494,656 (not used, so that every file has the same format) |

These figures are for the generator's art. The masters shipped since v1.1.1 (with the art edit above,
the same oxipng/zopfli settings) total **1,560,965 bytes** (v1.1, index stroke 115: 1,579,399).

All optimised files were checked to be pixel-identical to the rsvg output. The smallest file is
AD.png at 7,378 B and the largest is QH.png at 96,311 B. `SHA256SUMS.txt` covers `svg/` and
`master/`.

## 3. Geometry measured on the masters (use these numbers in code)

- **Outline:** black `#000000`, 1 SVG unit = **1.667 px** at the master. It sits on the outer edge:
  pixel 0 is (0,0,0) and pixel 1 is (86,86,86), which is 2/3 coverage over white. The same applies
  on all four sides (measured at x=0..3, y=280 and at x=200, y=0..5 and the mirrored positions).
  At display height h it is h/336 px thick, so only **0.29 px at h=96** and 0.89 px at h=300. It
  almost disappears when the card is scaled down (see `preview_sizes.png`). **Recommendation:** draw
  a crisp 1-px dark outline (rounded rectangle) in code after scaling, at every size. That matches
  XP's 1-px black frame.
- **Corner radius:** the SVG `rx` is 12 units, which is the stroke centre-line.
  - Outer radius = 12.5 units = **20.8 px at the master**.
  - Measured on the alpha edge: the first pixel with alpha ≥ 128 is at x=16 on row 0, x=11 on row 2,
    x=6 on row 6 and x=2 on row 12, and the edge reaches x=0 at row 16. This fits a circle of
    R = 20.8 centred at (20.8, 20.8).
  - As formulas at display height h: outer radius r ≈ **0.0372·h** (= 12.5/336·h), centre-line
    radius ≈ 0.0357·h, or ≈ 0.052·w as a share of width.
  - Examples: h=96 → 3.6 px; 150 → 5.6; 200 → 7.4; 260 → 9.7; 300 → 11.2.
  - Use the same radius for the empty free-cell and foundation outlines.
- **Index** (master px; measured with the generator's stroke 80; the shipped masters use 130, see §2):
  - Rank glyph bbox, e.g. 7♦: x 20–69, y 20–102 (83 px tall, about 50 px wide; stroke 6.7 px).
  - Suit glyph below it: about 46×56 px at y 118–174.
  - The bottom-right index is the same, rotated 180°.
- **Court frame:** `#4444FF` line, 1.67 px in the generator's art (the shipped masters: `#222233`,
  2.5 px, see §2). The bbox on K♥ is x 79–320, y 86–473 px (144×232 units,
  centred). The court figure is 1300×2000 artwork drawn into a 144×232 box with
  `preserveAspectRatio="none"`, so it is compressed horizontally by 4.5 %. This is not noticeable.
- **Court palette:** `#44F` blue, `#FC4` gold, red, black and white.

## 4. Contact sheet and target-size preview

- `contact_sheet.png` (1412×600): 13 columns (A…K) × 4 rows (C, D, H, S), each card 100×140, on a
  RGB(0,128,0) background, scaled with Lanczos.
- `preview_sizes.png` (1430×1054): A♠ 7♦ 10♣ J♥ Q♠ K♥ at heights **96, 150, 200, 260, 300**. The
  widths are 69, 107, 143, 186 and 214, keeping the 5:7 aspect. Scaled with ImageMagick
  `-filter Lanczos -resize x<h>` on a RGB(0,128,0) background.
- Zoomed 96-px comparison: `../research/art/z96_3x.png`. Row 1 is Lanczos, row 2 is Box (area
  average), row 3 is the original XP bitmaps.

**Assessment:**
- **96 px tall:** every index (A, 7, 10, J, Q, K plus the suit) is clearly readable and about the
  same size as XP's. The strokes are thinner than XP's bold 2-px pixel font: about 1 px, softly
  anti-aliased. Pips are clean. Courts are recognisable, but the thin blue frame and fine blue
  linework make them look paler than XP's high-contrast courts. The outline is very faint (see
  section 3), and the card reads mainly as white against green.
  - Lanczos is slightly sharper than Box, with no visible ringing. Box is a little softer. Either
    is acceptable; I would use Lanczos or a good separable filter.
- **150 px and up:** everything is crisp and the courts read well. I saw no artefacts: no seams, no
  fringes on the alpha edge and no gaps in the corners.
- **200–300 px** (the 1080p range): excellent.
- The 400×560 master covers display sizes up to 560 px tall without upscaling.

## 5. Quirks and notes for the renderer

1. **Aspect:** 0.714 against XP's 0.740. A 96-px-tall card is 68.6 px wide instead of 71, which is
   3.4 % narrower. Either derive the layout from the 5:7 aspect, or stretch horizontally by 3.6 %
   if exact XP proportions matter. The stretch is visually negligible but distorts pips slightly.
2. **Alpha:** the PNGs are straight-alpha RGBA. Win32 `AlphaBlend` with `AC_SRC_ALPHA` needs
   premultiplied BGRA, so premultiply after decoding and after scaling. Scale in premultiplied space,
   or the corner edges get dark fringes.
3. **Outline:** draw the outline in code, as in section 3. The master's outline cannot survive heavy
   downscaling.
4. **Medium index:** this option also makes the pips smaller and the court frame narrower than the
   generator's Normal setting. That is intended, because it makes the cards look more like XP.
5. **Ace of Spades:** "Fancy" is a large spade with a decorative dashed outline. The XP A♠ is a
   large spade with interlaced ornament. Knoll's `ace_of_spades.svg` (public domain) is the closest
   ornate alternative if we want it (`../research/art/knoll13/SVG-cards-1.3/ace_of_spades.svg`), but
   it has a different border and index style.
6. **XP reference facts measured from `cards.dll`** (bitmap resources extracted with pefile into
   `../research/art/xp/`):
   - IDs 1–52 are 71×96 faces, ordered by suit (clubs 1–13, diamonds 14–26, hearts 27–39,
     spades 40–52), each running A→K. Confirmed visually in `xp_grid2x.png`.
   - The index rank glyph is 12 px tall (y 4–15) with 2-px strokes, and the suit symbol below it is
     about 9 px wide.
   - The bitmap corners are stepped: pixels (0,0), (1,0) and (0,1) are white and (2,0) and (1,1)
     are frame-coloured.
   - In the resource, the 1-px frame of red cards is **red** (255,0,0), for example bitmap 27 = A♥.
     Black cards have a black frame.
   - UNVERIFIED: whether cdtDraw overpaints the frame black at runtime. Someone should check the real
     game before deciding the outline colour of red cards in the clone.
7. **Regenerating the art:** `tools/make_assets.sh cards check` (the edit of §2, rsvg-convert, oxipng).
   The original procedure, before the edit:
   - `for f in svg/??.svg; do rsvg-convert -h 560 $f -o raw/$(basename $f .svg).png; done`
   - Then run oxipng with zopfli: `pyoxipng` in the venv,
     `oxipng.optimize(src, dst, level=6, strip=StripChunks.all(), color_type_reduction=False,
     bit_depth_reduction=False, palette_reduction=False, grayscale_reduction=False,
     deflate=Deflaters.zopfli(15))`.
   - The contact sheet and preview come from `../research/art/sheets.py <art_dir> <tmp_dir>`.

## Large Print (2026-10-02)
`res/common/cards-large/<R><S>.png` (Options > Extras "Large print cards", both games; RCDATA 1300 + card) are
the same SVGs and art edit laid out by stacklab variant `XPLIKE_BITTER_HYBRID_LARGE`
(`tools/make_assets.sh cards-large`): the Bitter rank 1.45x (rank_scale 0.957; ink 0.026-0.192 ch, cap
0.155 ch, left ink margin as the normal set's), the index suit beside the rank (suit_mode side, ink 0.9 of
the cap, centred on it, a fixed column at x -39.7 units), the hybrid pip arrangements with body pips 0.8 x 14/96 =
0.117 ch (pip_h 0.14583, pip_gain 0.8; at 0.8 x 15/96 the four pips of a 9's or 10's side column ran
together at 72-140 px: `tests/engine/test_engine.c` counts them) compressed into 0.23-0.77 ch (`pip_band`), the court frame from 0.23 ch with the picture's aspect kept (court_frame
[0.26996, 0.23]), the court pip 0.8x, the aces' pip 0.156 ch as the normal set. Shown with a face-up column
step of round(21 ch / 96) (variant key "steps", used by `stacklab.py glance` / `sheets`); the glance metric
and the decision are in docs/DESIGN.md "Large Print cards (2c)". 52 files, 1,402,889 bytes (oxipng, zopfli
15).

## Stacked-legibility layout (2026-10-02)
The masters are generated with `tools/crisplab/stacklab.py svg --layout <XPLIKE>` (see
`tools/crisplab/candidates/stack.json` and docs/ROADMAP.md 2e): rank glyph fitted inside the Solitaire
strip (15/96 of the card) with `rank_unclip` (no flattened tops/bottoms), suit under the rank, pips raised
to XP's positions and the court picture enlarged so every stacked strip shows rank + suit like XP.
