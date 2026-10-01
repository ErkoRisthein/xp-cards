# Round 2: refine (20 candidates; rounds/r2.json; out/r2/)

The new art for all candidates except `current` is index 130 with court linework #223 at 1.6x
("art").

## Findings
- **The art is the single largest improvement.** `art box` (box filter, new art) against
  `current`: index acutance at h96 rises from 44.5 to 45.7, and court ink from 7.5% to 13.0%
  (25% at h257). The courts stop looking pale blue (crops/full_h96_x2.png). It needs no code at
  all, only regenerated masters.
- **Dark-biased resampling ranks first on the metrics.** At h96, `art box sg.7` scores acutance
  47.5 / halo 0.38 / halo_vis 1.2%, and `art box sg.6` scores 48.2 / 0.50 / 1.5%. Compare
  `art box sep.4r` at 48.2 / 1.30 / 4.1% and `art l3 sep.3r` (the round-0 hypothesis) at
  48.2 / 1.56 / 4.0%. At equal acutance, sg has about a third of the halo. Visually, pips and index
  glyphs become solid like XP's, and courts gain punch without noise. Faces with dense hatching
  (KH) go a little greyer.
- **Sharpening on top (sg.7 + sep.3r)** gives the crispest index (49.2) but adds visible crunch in
  the court hatching at h96 (crops/centre_h96_x3.png). Use only a light amount, if any.
- **Lanczos-3** behaves the same at small sizes as box with a little sharpening, but keeps a halo
  of about 0.5 at h257-300 where nothing needs fixing, and costs about 4-5x the taps. PRUNED as a
  base, except for one finalist that represents the round-0 hypothesis.
- **tone_gamma 1.3 (post LUT)** darkens AA and shifts flat colours together. Its halo_vis of 4.6%
  is the highest; it is a colour change, not crispness. PRUNED.
- **Courts:** the difference between 1.4x, 1.6x and 1.8x is modest at h96. 1.8x is a little heavy
  at h257 (hair hatching), so 1.6x stays. The size-adaptive version (1.8x up to h140, then 1.3x)
  is barely distinguishable from a flat 1.6x and would need a second set of 12 court masters.
  PRUNED. Frame 1.5 units #223 or #000 replaces the faint light-blue picture frame, which looks
  unfinished next to the dark linework. Chosen: #223 1.5. Gold #FC4 -> #FB2 gives +5 chroma that
  is barely visible. PRUNED, to keep the art's palette.
- **Index:** 145 is boldest and most legible at h72, but "10" starts to crowd at h72-96. 130 has
  XP's weight (1.9 px strokes at h96 against XP's 2 px) and stays clean at h257. Chosen: 130.
- The thin court strokes cannot be split into outlines and hatching by width: the 6-unit path
  holds both outlines and hair hatching, and the 3-unit path holds fine details. So there is no
  outline-only emboldening.

## Carried into round 3
Art3 = index 130, courts #223 1.6x, picture frame #223 1.5. Filters: box with sg 0.5-0.7
(constant or ramped by the downscale factor), box with sg and a light sep sharpen, box with sep
alone, and the hypothesis L3 + sep.3r.
