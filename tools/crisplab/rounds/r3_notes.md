# Round 3: finalists head-to-head plus artefact hunt (11 candidates; rounds/r3.json; out/r3/)

Art3 = index 130, court linework #223 at 1.6x, court picture frame #223 at 1.5 units. All
filtering is ramped by the downscale factor f = 560/ch with t = clamp((f-2)/2, 0, 1): full
strength up to ch 140, nothing from ch 280 up.

## Artefact checks (crisplab.py artefacts; out/r3/artefacts.csv)
- Shape and alpha are identical to `current` for every candidate and size (alpha_diff 0). The
  table outside the card is untouched (outside_diff 0). There is no ringing on the green or
  outside the rounded corners: the art is opaque and the shape and frame are analytic and drawn
  after resampling.
- Red pips show no hue shift (|G-B| = 0) and no dark fringes (red_fringe equals the vector
  reference at every size). Overshoot on white is clamped away (white cannot go brighter), so
  there is no light halo around pips.
- **Court noise** (HF energy relative to ideal AA) at h96: box 1.04, sg family 1.08-1.10,
  sharpened variants 1.30 (sg.6r+sep.2r), 1.34 (sg.7+sep.25r), 1.35 (L3+sep.3r) and
  1.46 (box+sep.4r). The sharpened ones look crunchy and slightly moiréd on JD's cross-hatch at
  h96-128. They are clean at h160 and up (crops_m/).

## Picks
- Sharpened family: `art3 box sg.6r sep.2r` dominates `box sep.4r`, `l3 sep.3r` (the round-0
  hypothesis) and `box sg.7 sep.25r`. At h96 its acutance is 49.8 against 48.7, 48.7 and 49.5;
  its halo is 1.09 against 1.41, 1.64 and 1.12; its noise is 1.30 against 1.46, 1.35 and 1.34.
  It is identical to box at h>=280, while L3 keeps ringing there (halo 0.66 at h257) and costs
  about 4.4-5.3x the taps.
- Dark-bias family: `art3 box sg.5r` is the crispest with no noise penalty (h96: acutance 49.4,
  halo 0.76, noise 1.10). It is not heavy at h160 (crops_160/). sg.6r and sg.65 are in-between
  versions of it.
- The C prototype (cproto/resample_card.c: uint16 power-space box, 4.12 sharpening taps, LUTs)
  matches the numpy model to within 1 LSB for both picks at h72-257. Cost against the current box
  is about 1.45-1.7x for the 52-card rebuild.

## Finalists (blind: final/)
current | art-only (art3 + box) | art+darkbias (art3 + box sg 0.5 ramped) |
art+darkbias+sharpen (art3 + box sg 0.6 + sep 0.2, both ramped)
