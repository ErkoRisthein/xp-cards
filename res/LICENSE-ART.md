# Card artwork licence

## Card faces in `res/cards-svg/` and `res/cards/`, 

**Source:** "SVG playing cards" by Adrian Kennard (RevK), generated with the author's online generator.
- Project page (primary source): https://www.me.uk/cards/
- Exact generator request used (zip of SVGs):
  `https://www.me.uk/cards/makeadeck.cgi?size=poker&ace=Fancy&ace1=&ace2=&qr=&back=Diamond&value=0&super=1&pip=1&wider=on&zip=Download`
  (copy in `res/cards-src/GENERATOR_URL.txt`; the downloaded zip is kept as `res/cards-src/revk_generator_output.zip`,
  sha256 `f74072710820e6f554f04ae6fc123050097285ec537691f53b5bf1caa3d9e716`, fetched 2026-10-01)
- Snapshot of the project page as fetched 2026-10-01: `res/cards-src/me.uk_cards_page_2026-10-01.html`
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
A full copy of the CC0 1.0 legal code is in `res/cards-src/CC0-1.0-legalcode.txt`.

Our Ace of Spades has no link on it, because we generated it with the `ace1`, `ace2` and `qr` fields
left blank. The author says the link is "not a requirement".

**Caveat:** the generator program on Codeberg (`makecards.c` etc.) is GPL-3.0, as its `LICENSE` file
shows. We do not use or ship the program. We only use its output, the card images, and the author
and copyright holder explicitly dedicates those to the public domain under CC0. The court figures
are based on 19th-century Goodall & Son designs, which are themselves public domain because of their
age.

**Attribution (optional, as a courtesy):** "Card faces: SVG playing cards by Adrian Kennard
(https://www.me.uk/cards/), CC0 1.0."

## Candidates evaluated and not used (licence evidence)

| Set | Licence as verified | Evidence |
|---|---|---|
| Wikimedia Commons "Playing card heart A.svg" etc. (the "SVG playing cards 2" set) | **GFDL + CC BY-SA 3.0. Rejected.** The author is en:User:Cburnett, **not** Dmitry Fomin, and the set is not public domain. | https://commons.wikimedia.org/wiki/File:Playing_card_heart_A.svg, wikitext `{{Self|GFDL|Cc-by-sa-3.0-migrated}}` |
| Dmitry Fomin, "English pattern <rank> of <suit>.svg" (52 files) and "English pattern playing cards deck.svg" | CC0. Usable, but not chosen for visual reasons (see ART.md). | e.g. https://commons.wikimedia.org/wiki/File:English_pattern_king_of_hearts.svg, wikitext `{{self|cc-zero}}`, author `[[User:Dmitry Fomin|Дмитрий Фомин (Dmitry Fomin)]]`, date 2017-02-24. The full deck file is also `{{self|Cc-zero}}`. |
| Byron Knoll, "vector-playing-cards" 1.3 | Public domain. Usable; this is the runner-up. | Google Code archive project.json (https://storage.googleapis.com/google-code-archive/v2/code.google.com/vector-playing-cards/project.json): "These images are released into the public domain - attribution is appreciated but not required." The GitHub mirror notpeter/Vector-Playing-Cards README says "released into the public domain or optionally licensed under the WTFPL". |
| David Bellot SVG-cards, Chris Aguilar Vector Playing Cards | LGPL. Rejected per the brief and not downloaded. | Licence as stated in the brief (UNVERIFIED here). |
