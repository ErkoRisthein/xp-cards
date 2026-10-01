#!/usr/bin/env python3
"""Rasterise Solitaire HD's 12 card backs: res/solitaire/backs-src/<id>_<name>.svg -> res/solitaire/backs/<id>_<name>.png

    make_backs.py <backs-src dir> <out dir>

Art edit before rasterising (ours, CC0 like the rest; the SVGs here stay as generated): the Diamond,
Illusion and Maze designs draw their pattern inset 12 to 28 units (of 240 x 336) from the card's edge, on
the background colour, which is white for Sky and Rose. XP's backs run to the edge, and the 3-D edges of the
stock and the face-down strips of the tableau (3 px at 96 px card height, layout.md §2.2) show their
colour, so the pattern is scaled about the centre to an inset of INSET units: the strips show it.
The Diamond lattice (a 6-unit period: 1.7 px at XP's 96-px card height, 2 px at 115) is drawn twice as
large: the fine lattice beat against the pixel grid at window sizes between XP's and 1080p (moire bands
in the card resampler's box filter); at twice the period it stays a clean texture from 96 px up.

Each back is rendered like the card faces (rsvg-convert, 400x560 = 5:7 at 1 SVG unit = 1.6667 px;
docs/card-art.md §2), then made opaque along its edge: the generator's 1-unit black outline and the
transparent rounded corners take the colour just inside the outline (the band the engine's card set
cleans anyway, src/engine/cardset.c clean_master: 3.5 px at 560, corner radius 0.0372 h), so the
master is a plain opaque RGB image. The engine draws its own crisp frame and rounded shape after
scaling, and Solitaire HD's dialog thumbnails (sol_back_image) need no cleaning of their own.

The PNG is written RGB (no alpha), then optimised losslessly with oxipng (which may store it as a
palette image) when the module is available (tools/make_assets.sh runs this with $OXIPNG_PYTHON).
Requires Python 3 + Pillow and rsvg-convert on PATH.
"""
import glob, io, os, re, subprocess, sys
from PIL import Image

W, H = 400, 560
INSET = 5.0                     # pattern inset after the edit, SVG units (the generator's: 12)

def edit(svg):
    """Scale the inset pattern block of the Diamond / Illusion / Maze designs about the centre."""
    rects = re.findall(r'<rect([^>]*)>', svg)
    m = re.search(r'\bx="(-[\d.]+)" y="(-[\d.]+)"', rects[1]) if len(rects) > 1 else None
    if not m:
        return svg                          # Goodall, Arrows: one card rect, already full bleed
    x0, y0 = -float(m.group(1)), -float(m.group(2))     # the block's inset from the centre (108, 156 / 100, 140)
    sx, sy = (120 - INSET) / x0, (168 - INSET) / y0
    diamond = '<pattern id="B2" width="6" height="6" patternUnits="userSpaceOnUse">'
    if diamond in svg:
        # twice the period after the block's scale: 12 units = exactly 20 master px in x and y, in phase
        # with the pixel grid (the centre is pixel (200, 280)), so the master stays a small PNG
        svg = svg.replace(diamond, '<pattern id="B2" width="6" height="6" patternUnits="userSpaceOnUse" '
                                   'patternTransform="scale(%.6f %.6f)">' % (2 / sx, 2 / sy))
    end = svg.index('</rect>') + len('</rect>')          # the card rect (fill, outline) stays
    close = svg.rindex('</svg>')
    return svg[:end] + '<g transform="scale(%.6f %.6f)">' % (sx, sy) + svg[end:close] + '</g>' + svg[close:]

def render(svg_path):
    svg = edit(open(svg_path, encoding='utf-8').read())
    p = subprocess.run(['rsvg-convert', '-w', str(W), '-h', str(H)], input=svg.encode('utf-8'),
                       capture_output=True, check=True)
    return Image.open(io.BytesIO(p.stdout)).convert('RGBA')

def clean(im):
    """Opaque edge band + corners in the colour just inside the band (half-way down the left edge)."""
    px = im.load()
    band = 3.5 * H / 560.0
    r = 0.0372 * H
    ri = r - band
    def edge(x, y):
        cx, cy = x + 0.5, y + 0.5
        dx = r - cx if cx < r else (cx - (W - r) if cx > W - r else 0)
        dy = r - cy if cy < r else (cy - (H - r) if cy > H - r else 0)
        return (cx < band or cy < band or cx > W - band or cy > H - band or
                (dx > 0 and dy > 0 and dx * dx + dy * dy > ri * ri))
    sx = int(band + 1.5)
    fr, fg, fb, fa = px[sx, H // 2]
    fill = tuple(int(round(c * fa / 255 + 255 * (1 - fa / 255))) for c in (fr, fg, fb))   # over white
    out = Image.new('RGB', im.size)
    op = out.load()
    for y in range(H):
        for x in range(W):
            if edge(x, y):
                op[x, y] = fill
            else:
                rr, gg, bb, aa = px[x, y]
                k = 255 - aa                                     # straight alpha over white
                op[x, y] = (rr * aa // 255 + k, gg * aa // 255 + k, bb * aa // 255 + k) if aa < 255 else (rr, gg, bb)
    return out

def optimise(path):
    try:
        import oxipng
    except ImportError:
        return
    oxipng.optimize(path, path, level=6, strip=oxipng.StripChunks.all(),
                    deflate=oxipng.Deflaters.zopfli(15))

def main():
    src, dst = sys.argv[1], sys.argv[2]
    os.makedirs(dst, exist_ok=True)
    for svg in sorted(glob.glob(os.path.join(src, '[0-9][0-9]_*.svg'))):
        name = os.path.splitext(os.path.basename(svg))[0]
        out = os.path.join(dst, name + '.png')
        clean(render(svg)).save(out, optimize=True)
        optimise(out)
        print('%s (%d bytes)' % (out, os.path.getsize(out)))

if __name__ == '__main__':
    main()
