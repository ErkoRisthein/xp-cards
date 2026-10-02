# Artwork licences

## Card faces: `res/common/cards-svg/` (SVG), `res/common/cards/` and `res/common/cards-large/` (400x560 PNG masters)

**Source:** "SVG playing cards" by Adrian Kennard (RevK), generated with the author's online generator.
- Project page (primary source): https://www.me.uk/cards/
- Exact generator request used (zip of SVGs):
  `https://www.me.uk/cards/makeadeck.cgi?size=poker&ace=Fancy&ace1=&ace2=&qr=&back=Diamond&value=0&super=1&pip=1&wider=on&zip=Download`
  (copy in `res/common/cards-src/GENERATOR_URL.txt`). The zip it returned on 2026-10-01 (sha256
  `f74072710820e6f554f04ae6fc123050097285ec537691f53b5bf1caa3d9e716`) is not shipped; its 52 face SVGs
  are `res/common/cards-svg/<R><S>.svg`.
- Snapshot of the project page as fetched 2026-10-01: `res/common/cards-src/me.uk_cards_page_2026-10-01.html`
- `res/common/cards/<R><S>.png` are rasterised from those SVGs (`rsvg-convert -h 560`, then lossless oxipng;
  `tools/make_assets.sh cards`, see `docs/card-art.md`) after a small edit by
  `tools/edit_card_svg.py`: a bolder rank index (stroke 80 → 130) and, on the court cards, darker
  (`#223`) and 1.6x wider court linework and a darker court picture frame. These changes are ours and
  are dedicated to the public domain under CC0 1.0 as well; the SVGs in `res/common/cards-svg/` are kept as
  generated.
- `res/common/cards-large/<R><S>.png` (the "Large print cards" option) are rasterised the same way from the
  same SVGs with another layout (`tools/make_assets.sh cards-large`: stacklab variant
  XPLIKE_BITTER_HYBRID_LARGE, a 1.45x index with its suit beside the rank, smaller pips and court picture);
  the same licences (CC0 art, Bitter rank glyphs below).
- Independent archived copy: http://web.archive.org/web/20260829054033/https://www.me.uk/cards/
- Generator source code: https://codeberg.org/RevK/SVG-playing-cards

**Licence: CC0 1.0 Universal (public domain dedication).** This is the verbatim text of the project page (2026-10-01):

> "For hundreds of years people have been [badly] copying playing cards (see this article for more
> details) and I am proud to be part of that long standing tradition and offer my SVG playing card
> sets, well, a lot of them in fact, to the public domain. The court cards are based on 19th Century
> Goodall & Son designs."
>
> "No attribution required. Whilst I'd like a credit, and I would appreciate it if the link on the
> Ace of Spades was left intact, that is not a requirement. You can do what you like with these designs."
>
> "© Copyright 2018 Adrian Kennard Released under CC0 Public Domain licence."

The page links the CC0 legal code at https://creativecommons.org/publicdomain/zero/1.0/legalcode
and shows the CC public-domain mark (`https://i.creativecommons.org/p/mark/1.0/88x31.png`).
A full copy of the CC0 1.0 legal code is in `res/common/cards-src/CC0-1.0-legalcode.txt`.

Our Ace of Spades has no link on it, because we generated it with the `ace1`, `ace2` and `qr` fields
left blank. The author says the link is "not a requirement".

**Caveat:** the generator program on Codeberg (`makecards.c` etc.) is GPL-3.0, as its `LICENSE` file
shows. We do not use or ship the program. We only use its output, the card images, and the author
and copyright holder explicitly dedicates those to the public domain under CC0. The court figures
are based on 19th-century Goodall & Son designs, which are themselves public domain because of their
age.

**Attribution (optional, as a courtesy; wording from `docs/DESIGN.md`):** "Card faces: SVG playing
cards by Adrian Kennard — https://cards.revk.uk (CC0)". The exe's version info carries it in `Comments`.

## King art: `res/freecell/king/` (our derivative, CC0 1.0)

`res/freecell/king/king_right.png`, `king_left.png` and `king_smile.png` (1024x1024 RGBA, the busts shown in
the king box and after a win) are rendered from `res/freecell/king/src/king_{right,left,smile}.svg`, which
`res/freecell/king/src/derive_kings.py` derives from the **King of Spades court figure of the RevK card set
above** (`res/common/cards-svg/KS.svg`, CC0 1.0, after the public-domain Goodall & Son pattern):

- the upper figure cropped to a square bust; the card's dividing line removed; outlines darkened and
  thickened; an explicit white backing added; the crown's open top outlined;
- our own additions: the blue mantle with gold hem at the bottom left, and for `king_smile` new brows,
  lower lids and a smiling mouth; `king_left` is the mirror image of `king_right`.

No part of Microsoft's FreeCell bitmaps ("KingBitmap", "KingLeft", "KingSmile") was used; they were
only looked at for composition (a king's bust looking right, left, and smiling). We dedicate our
changes to the public domain under **CC0 1.0** as well, so the king art is CC0 as a whole.

## Program icon `res/freecell/freecell.ico` and cursor `res/freecell/downarrow.cur` (our own work, CC0 1.0)

- The icon (48, 32, 24 and 16 px; 32-bpp, 8-bpp and 4-bpp) is our own composition, made by
  `res/freecell/icon/make_icon.py`: our king bust above (CC0) in front of a card drawn with RevK's diamond
  pip shape (CC0); its SVG source is `res/freecell/icon/icon_card.svg`. The 16x16 image is an original pixel
  drawing kept as text in `make_icon.py`. It only follows the idea of XP FreeCell's icon (a king's
  head in front of a card) and contains no Microsoft pixels.
- The "DownArrow" cursor is generated from simple geometry by `res/freecell/icon/make_cursor.py` (a 4-px shaft,
  a broad head, a 2-px outline, hotspot at the tip); it is not a copy of XP's bitmap.
- Both are dedicated to the public domain under **CC0 1.0** (legal code: `res/common/cards-src/CC0-1.0-legalcode.txt`).

`tools/make_assets.sh` regenerates all of the above from these sources.

## Candidates evaluated and not used (licence evidence)

| Set | Licence as verified | Evidence |
|---|---|---|
| Wikimedia Commons "Playing card heart A.svg" etc. (the "SVG playing cards 2" set) | **GFDL + CC BY-SA 3.0. Rejected.** The author is en:User:Cburnett, **not** Dmitry Fomin, and the set is not public domain. | https://commons.wikimedia.org/wiki/File:Playing_card_heart_A.svg, wikitext `{{Self|GFDL|Cc-by-sa-3.0-migrated}}` |
| Dmitry Fomin, "English pattern <rank> of <suit>.svg" (52 files) and "English pattern playing cards deck.svg" | CC0. Usable, but not chosen for visual reasons (see `docs/card-art.md`). | e.g. https://commons.wikimedia.org/wiki/File:English_pattern_king_of_hearts.svg, wikitext `{{self|cc-zero}}`, author `[[User:Dmitry Fomin|Дмитрий Фомин (Dmitry Fomin)]]`, date 2017-02-24. The full deck file is also `{{self|Cc-zero}}`. |
| Byron Knoll, "vector-playing-cards" 1.3 | Public domain. Usable; this is the runner-up. | Google Code archive project.json (https://storage.googleapis.com/google-code-archive/v2/code.google.com/vector-playing-cards/project.json): "These images are released into the public domain - attribution is appreciated but not required." The GitHub mirror notpeter/Vector-Playing-Cards README says "released into the public domain or optionally licensed under the WTFPL". |
| David Bellot SVG-cards, Chris Aguilar Vector Playing Cards | LGPL. Rejected per the brief and not downloaded. | Licence as stated in the brief (UNVERIFIED here). |

## Rank index glyphs (2026-10-02)
The rank index letters/digits on the card faces are outlines taken from **Bitter** (weight 800),
© 2011 The Bitter Project Authors (https://github.com/solmatas/BitterPro), licensed under the
SIL Open Font License 1.1 — full text in `tools/crisplab/candidates/OFL-Bitter.txt`. The outlines are
frozen in `tools/crisplab/candidates/rank_bitter800.json` and rendered into the card art by
`tools/crisplab/stacklab.py` (layout variants XPLIKE_BITTER*, also the Large Print faces). The OFL permits embedding glyphs in
artwork; the font itself is not redistributed as a font.
