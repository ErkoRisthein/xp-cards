#!/usr/bin/env python3
"""Build res/solitaire/solitaire.ico (XP-compatible), Solitaire HD's program icon.

    make_icon.py <out dir for the icon SVG> <out .ico>

Design (our own, in the spirit of XP Solitaire's icon, an opened card box): a light carton seen from
the front-left with its lid folded back, a window in its front showing a blue card back (our Sky back's
diamond lattice), and two cards fanned out of the top, each with a red diamond pip (the RevK pip shape,
CC0). The SVG is written to <out dir>/icon_box.svg and rendered with rsvg-convert at 48, 32 and 24 px;
16 px is an original pixel drawing (PIXEL16), where the art would turn to mush.

Each size (48, 32, 24, 16) is stored three times, uncompressed (no PNG entries: XP cannot read them):
32 bpp with alpha, 8 bpp (adaptive palette) and 4 bpp (VGA palette, drawn in flat VGA colours), each
with a 1-bpp AND mask. The
ICO writer and the renderer are FreeCell HD's (res/freecell/icon/make_icon.py).
Requires Python 3 + Pillow and rsvg-convert on PATH.
"""
import os, sys
sys.dont_write_bytecode = True    # importing FreeCell HD's tool must not leave a __pycache__ there
from PIL import Image

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'freecell', 'icon'))
from make_icon import DIAMOND, SIZES, render, write_ico   # noqa: E402  (FreeCell HD's icon tooling)

def pip(x, y, size, rot=0):
    k = size / 1000.0
    return ('<path fill="#e00000" transform="translate(%g %g) rotate(%g) scale(%g)" d="%s"/>'
            % (x, y, rot, k, DIAMOND))

def card(cx, cy, rot):
    """A card 15 x 21 centred at (cx, cy), rotated rot degrees, with one pip near its top."""
    return ('<g transform="rotate(%g %g %g)">' % (rot, cx, cy) +
            '<rect x="%g" y="%g" width="15" height="21" rx="1.6" fill="#fff" stroke="#2a2a2a" stroke-width="0.9"/>'
            % (cx - 7.5, cy - 10.5) + pip(cx, cy - 5.2, 6.4) + '</g>')

def box_svg():
    lattice = ''.join('<path d="M%g %gl2.2 2.2l-2.2 2.2l-2.2 -2.2z" fill="#6f9cf2"/>' % (x, y)
                      for y in range(23, 45, 4) for x in range(13 + (y // 4) % 2 * 2, 33, 4))
    return (
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 48 48" width="48" height="48">\n'
        '<title>Solitaire HD icon</title>\n'
        '<defs>'
        '<linearGradient id="front" x1="0" y1="0" x2="1" y2="1">'
        '<stop offset="0" stop-color="#ffffff"/><stop offset="1" stop-color="#b7d0f2"/></linearGradient>'
        '<linearGradient id="side" x1="0" y1="0" x2="0" y2="1">'
        '<stop offset="0" stop-color="#93b6ea"/><stop offset="1" stop-color="#5d86c8"/></linearGradient>'
        '<linearGradient id="back" x1="0" y1="0" x2="0" y2="1">'
        '<stop offset="0" stop-color="#2f6fe6"/><stop offset="1" stop-color="#0d2f8f"/></linearGradient>'
        '<linearGradient id="lid" x1="0" y1="1" x2="0" y2="0">'
        '<stop offset="0" stop-color="#c9dcf6"/><stop offset="1" stop-color="#ffffff"/></linearGradient>'
        '<clipPath id="win"><rect x="13.4" y="23.4" width="16.2" height="18.7" rx="1"/></clipPath>'
        '</defs>\n'
        # the lid, folded back behind the box's top edge
        '<path d="M11.5 17L17 4.5L42 4.5L38.5 13Z" fill="url(#lid)" stroke="#1f3f7a" stroke-width="1" '
        'stroke-linejoin="round"/>\n'
        # two cards fanned out of the top
        + card(19, 13.5, -14) + card(27.5, 12.5, 9) + '\n'
        # the box: right side, front, the window with a card back
        '<path d="M33.5 17L39 13L39 41.5L33.5 46Z" fill="url(#side)" stroke="#1f3f7a" stroke-width="1" '
        'stroke-linejoin="round"/>\n'
        '<rect x="9.5" y="17" width="24" height="29" rx="1.5" fill="url(#front)" stroke="#1f3f7a" stroke-width="1"/>\n'
        '<path d="M9.5 17.5L33.5 17.5" stroke="#ffffff" stroke-width="1" opacity="0.8"/>\n'
        '<rect x="13" y="23" width="17" height="19.5" rx="1.2" fill="url(#back)" stroke="#123a8c" stroke-width="0.8"/>\n'
        '<g clip-path="url(#win)" opacity="0.85">' + lattice + '</g>\n'
        '<path d="M13.5 23.5L29.5 23.5" stroke="#9cc0ff" stroke-width="0.8" opacity="0.7"/>\n'
        # the thumb notch of the box front
        '<path d="M17.5 17.2A4 3 0 0 0 25.5 17.2" fill="none" stroke="#1f3f7a" stroke-width="0.9"/>\n'
        '</svg>\n')

# 16 x 16: K navy outline, W white, w light blue, b mid blue, B deep blue, R red, S side blue, . transparent
PIXEL16 = """
....KKKK.KKKK...
...KWWWKKWWWK...
...KWRWKKWRWK...
..KKWWWKKWWWKK..
..KwwwwwwwwwwSK.
..KwwwwwwwwwwSK.
..KwKKKKKKKKwSK.
..KwKbBbBbBKwSK.
..KwKBbBbBbKwSK.
..KwKbBbBbBKwSK.
..KwKBbBbBbKwSK.
..KwKbBbBbBKwSK.
..KwKKKKKKKKwSK.
..KwwwwwwwwwwSK.
..KKKKKKKKKKKKK.
................
"""
PIXEL_COLOURS = {'.': (0, 0, 0, 0), 'K': (0x1f, 0x3f, 0x7a, 255), 'W': (255, 255, 255, 255),
                 'w': (0xd4, 0xe4, 0xfa, 255), 'b': (0x4a, 0x80, 0xee, 255), 'B': (0x1b, 0x45, 0xb8, 255),
                 'R': (0xe0, 0, 0, 255), 'S': (0x6d, 0x93, 0xd2, 255)}

PIXEL_VGA = dict(PIXEL_COLOURS, K=(0, 0, 128, 255), w=(255, 255, 255, 255), b=(0, 0, 255, 255),
                 B=(0, 0, 128, 255), R=(255, 0, 0, 255), S=(192, 192, 192, 255))

def pixel_art(text, colours=PIXEL_COLOURS):
    rows = text.split()
    im = Image.new('RGBA', (len(rows[0]), len(rows)))
    im.putdata([colours[c] for r in rows for c in r])
    return im

def vga_svg(svg):
    """The same drawing in flat VGA colours for the 4-bpp images (the gradients would quantise to teal
    and grey): white front, silver side and lid, a navy window with a blue lattice, navy outlines."""
    for old, new in (('url(#front)', '#ffffff'), ('url(#side)', '#c0c0c0'), ('url(#back)', '#000080'),
                     ('url(#lid)', '#c0c0c0'), ('#6f9cf2', '#0000ff'), ('#1f3f7a', '#000080'),
                     ('#123a8c', '#000080'), ('#e00000', '#ff0000'), ('#2a2a2a', '#000000'),
                     ('opacity="0.85"', ''), ('stroke="#9cc0ff"', 'stroke="none"')):
        svg = svg.replace(old, new)
    return svg

if __name__ == '__main__':
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    svg = box_svg()
    with open(os.path.join(sys.argv[1], 'icon_box.svg'), 'w', encoding='utf-8') as f:
        f.write(svg)
    full = {s: render(svg, s) for s in SIZES if s != 16}
    full[16] = pixel_art(PIXEL16)
    flat = {s: render(vga_svg(svg), s) for s in SIZES if s != 16}
    flat[16] = pixel_art(PIXEL16, PIXEL_VGA)
    write_ico(sys.argv[2], [(full[s], bpp) for bpp in (32, 8) for s in SIZES] + [(flat[s], 4) for s in SIZES])
