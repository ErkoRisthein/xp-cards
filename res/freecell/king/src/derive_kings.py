#!/usr/bin/env python3
"""Derive the FreeCell HD king busts (SVG) from the CC0 RevK King of Spades.

    derive_kings.py <res/common/cards-svg/KS.svg> <out dir>
        -> king_right.svg, king_left.svg, king_smile.svg (1024x1024, transparent background)

The court figure of KS.svg is six layers in a 1300x2000 box (gold, red, blue and black fills, then
thick and thin #44F outlines; white is the bare card). We take the upper figure as a square bust
in the spirit of XP's 32x32 "KingBitmap" (crown on top, face right of centre, robe at the bottom):
  * the card's dividing line is dropped, the open crown top gets an outline, and an empty area at
    the bottom left gets a blue mantle with a gold hem (our own addition);
  * outlines are darkened (#44F -> #22229A) and thickened (6 -> 8, 3 -> 4) so the bust still reads
    on the green table at 32 px;
  * the white areas get an explicit backing path: everything not reachable from the top edge through
    transparent pixels, shrunk 2 units so its edge hides under the outlines. It is computed from a
    raster (rsvg-convert + Pillow) and traced back to a polygon, so the SVGs are self-contained;
  * "left" mirrors the whole bust (XP's KingLeft also turns the head: hair on the other side);
  * "smile" replaces the stern mouth and brows with relaxed brows, raised lower lids and a smile.
Requires Python 3 + Pillow and rsvg-convert on PATH.
"""
import io, os, re, subprocess, sys
from PIL import Image, ImageDraw, ImageFilter

LINE, GOLD, BLUE = '#22229A', '#FC4', '#44F'
W_THICK, W_THIN = 8, 4                  # outline widths (KS.svg: 6 and 3)
CROP = (282, -18, 880)                  # x0, y0, side of the square, in figure units
OUT_PX = 1024

# ---- minimal SVG path parser (absolute output) ------------------------------------------------
TOK = re.compile(r'[MmLlHhVvCcSsQqTtAaZz]|[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?')
NARGS = dict(M=2, L=2, H=1, V=1, C=6, S=4, Q=4, T=2, A=7, Z=0)

def parse(d):
    """Split a path into subpaths; each is a list of (cmd, absolute args)."""
    toks = TOK.findall(d); i = 0; cmd = None; subs = []
    x = y = sx = sy = 0.0
    while i < len(toks):
        if toks[i].isalpha():
            cmd = toks[i]; i += 1
        C, rel = cmd.upper(), cmd.islower()
        if C == 'Z':
            subs[-1].append(('Z', [])); x, y = sx, sy
            if i < len(toks) and not toks[i].isalpha(): raise ValueError('number after Z')
            continue
        a = [float(v) for v in toks[i:i + NARGS[C]]]; i += NARGS[C]
        if C == 'M':
            if rel: a = [a[0] + x, a[1] + y]
            x, y = sx, sy = a
            subs.append([('M', a)]); cmd = 'l' if rel else 'L'
        elif C in 'HV':
            v = a[0] + ((x if C == 'H' else y) if rel else 0)
            if C == 'H': x = v
            else: y = v
            subs[-1].append(('L', [x, y]))
        elif C == 'A':
            if rel: a[5] += x; a[6] += y
            x, y = a[5], a[6]; subs[-1].append(('A', a))
        else:
            if rel: a = [v + (x if k % 2 == 0 else y) for k, v in enumerate(a)]
            x, y = a[-2], a[-1]; subs[-1].append((C, a))
    return subs

def fmt(v):
    s = ('%.2f' % v).rstrip('0').rstrip('.')
    return '0' if s == '-0' else s

def to_d(sub):
    return ''.join(c + ' '.join(fmt(v) for v in a) for c, a in sub)

# ---- source layers ------------------------------------------------------------------------------
def load(path):
    s = open(path, encoding='utf-8').read()
    layers = []
    for i in range(1, 7):
        body = re.search(r'<symbol id="SK%d"[^>]*>(.*?)</symbol>' % i, s, re.S).group(1)
        m = re.search(r'<path ([^>]*?)d="([^"]*)"', body)
        layers.append([m.group(1), parse(m.group(2)), re.findall(r'<use [^>]*>', body)])
    pip = re.search(r'<symbol id="SSK"[^>]*>(.*?)</symbol>', s, re.S).group(1)
    return layers, pip

def drop(layer, *starts):
    """Remove the subpaths of a layer that start at the given points."""
    def hit(sub):
        x, y = sub[0][1][:2]
        return any(abs(x - sx) < 0.5 and abs(y - sy) < 0.5 for sx, sy in starts)
    n = len(layer[1])
    layer[1] = [sb for sb in layer[1] if not hit(sb)]
    assert n - len(layer[1]) == len(starts), 'KS.svg changed: subpath not found'

def stroke(d, w, col=LINE, extra=''):
    return ('<path fill="none" stroke="%s" stroke-width="%g" stroke-linecap="round" '
            'stroke-linejoin="round"%s d="%s"/>' % (col, w, extra, d))

# ---- our additions --------------------------------------------------------------------------------
CROWN_TOP = stroke('M332.74 0.47L1112.59 0.63', W_THICK)

def mantle():
    """Blue mantle filling the bottom-left (empty on the card), with folds and a gold hem along
    the robe's left edge (which has no outline on the card)."""
    edge = ('M430.54 543.13C454.4 627.37 475.11 712.25 487.64 798.21L492.55 836.2L498.6 906.47'
            'L499.9 939.79L497.17 1056')
    shift = ' transform="translate(-15 0)"'
    return ('<path fill="%s" d="M520 540L436 543C420 548 400 556 372 560C330 585 300 625 282 668'
            'C262 715 250 770 240 830L240 1080L520 1080Z"/>' % BLUE
            + stroke('M436 543C420 548 400 556 372 560C330 585 300 625 282 668C262 715 250 770 240 830',
                     W_THICK)
            + stroke('M345 640C330 700 322 770 322 900M405 610C395 690 390 780 392 900', W_THIN)
            + stroke(edge, 34 + W_THICK, LINE, shift) + stroke(edge, 34 - W_THICK, GOLD, shift))

def smile(thick, thin):
    """Swap the stern brows/mouth for a smiling face; returns the new strokes."""
    drop(thick, (715, 295), (980, 275),                                # brows
         (815, 487.89), (810, 485), (920, 480), (840, 515))            # mouth, corners, lower lip
    drop(thin, (745.52, 332.84), (971.44, 331.77))                     # lower lids
    return ('<path fill="%s" stroke="%s" stroke-width="%g" stroke-linejoin="round" d="'
            'M712 288C735 266 768 250 800 250C822 250 840 256 852 264C842 262 822 258 800 258'
            'C772 258 742 270 712 288Z'
            'M984 274C970 260 954 254 940 254C924 254 906 258 892 266C904 262 924 260 940 261'
            'C956 262 970 266 984 274Z"/>' % (LINE, LINE, W_THICK)
            + stroke('M742 336C760 348 800 350 828 330M914 334C934 344 956 342 974 328', W_THIN)
            + stroke('M800 468C830 502 898 502 928 462', W_THICK)
            + stroke('M793 462C800 466 802 472 800 478M935 458C929 462 927 468 929 474', W_THIN)
            + stroke('M840 514C856 524 878 523 892 512', W_THICK))

# ---- composition ------------------------------------------------------------------------------------
def compose(layers, top, bottom):
    out = [bottom]
    for attrs, subs, uses in layers:
        if 'stroke=' in attrs:
            w = W_THICK if 'stroke-width="6"' in attrs else W_THIN
            attrs = re.sub(r'stroke="[^"]*"', 'stroke="%s"' % LINE, attrs)
            attrs = re.sub(r'stroke-width="[^"]*"', 'stroke-width="%g"' % w, attrs)
        out.append('<path %sd="%s"/>' % (attrs, ''.join(to_d(sb) for sb in subs)))
        out += [re.sub(r'\s*/?>$', '/>', u.replace('#SSK"', '#pip"')) for u in uses]
    out.append(top)
    return '\n'.join(out)

def svg_doc(defs, body, vb, px, mirror=False, meta=''):
    x0, y0, w, h = vb
    if mirror:
        body = '<g transform="translate(%g 0) scale(-1 1)">\n%s\n</g>' % (2 * x0 + w, body)
    return ('<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" viewBox="%g %g %g %g" width="%d" height="%d">\n'
            '%s<defs>%s</defs>\n%s\n</svg>\n' % (x0, y0, w, h, px[0], px[1], meta, defs, body))

def render(svg):
    p = subprocess.run(['rsvg-convert'], input=svg.encode(), capture_output=True, check=True)
    return Image.open(io.BytesIO(p.stdout)).convert('RGBA')

# ---- white backing -------------------------------------------------------------------------------------
def backing_mask(defs, body, vb, k, erode):
    im = render(svg_doc(defs, body, vb, (int(vb[2] * k), int(vb[3] * k))))
    a = im.getchannel('A').point(lambda v: 255 if v > 24 else 0)
    ImageDraw.floodfill(a, (0, 0), 128)                  # the outside, seeded at the top
    inside = a.point(lambda v: 0 if v == 128 else 255)
    return inside.filter(ImageFilter.MinFilter(2 * erode * k + 1))

def trace(mask):
    """Closed pixel-edge loops around the 255 region (inside on the right)."""
    w, h = mask.size; px = mask.load()
    ins = lambda x, y: 0 <= x < w and 0 <= y < h and px[x, y] > 127
    out = {}
    for y in range(h):
        for x in range(w):
            if px[x, y] <= 127: continue
            if not ins(x, y - 1): out.setdefault((x, y), []).append((x + 1, y))
            if not ins(x + 1, y): out.setdefault((x + 1, y), []).append((x + 1, y + 1))
            if not ins(x, y + 1): out.setdefault((x + 1, y + 1), []).append((x, y + 1))
            if not ins(x - 1, y): out.setdefault((x, y + 1), []).append((x, y))
    loops = []
    while out:
        start = cur = next(iter(out)); prev = None; loop = [start]
        while True:
            cands = out[cur]; nxt = cands[0]
            if len(cands) > 1 and prev:                  # pinch point: take the right turn
                dx, dy = cur[0] - prev[0], cur[1] - prev[1]
                if (cur[0] - dy, cur[1] + dx) in cands: nxt = (cur[0] - dy, cur[1] + dx)
            cands.remove(nxt)
            if not cands: del out[cur]
            prev, cur = cur, nxt
            if cur == start: break
            loop.append(cur)
        loops.append(loop)
    return loops

def simplify(pts, eps):
    """Douglas-Peucker for a closed loop (iterative)."""
    ring = pts + [pts[0]]
    far = max(range(len(pts)), key=lambda i: (pts[i][0] - pts[0][0]) ** 2 + (pts[i][1] - pts[0][1]) ** 2)
    keep = {0, far, len(ring) - 1}
    stack = [(0, far), (far, len(ring) - 1)]
    while stack:
        a, b = stack.pop()
        (x1, y1), (x2, y2) = ring[a], ring[b]
        dx, dy = x2 - x1, y2 - y1
        n = (dx * dx + dy * dy) ** 0.5 or 1e-9
        best, bi = eps, -1
        for i in range(a + 1, b):
            d = abs(dy * (ring[i][0] - x1) - dx * (ring[i][1] - y1)) / n
            if d > best: best, bi = d, i
        if bi >= 0:
            keep.add(bi); stack += [(a, bi), (bi, b)]
    return [ring[i] for i in sorted(keep)[:-1]]

def backing(defs, body, vb, k=2, erode=2):
    parts = []
    for loop in trace(backing_mask(defs, body, vb, k, erode)):
        if len(loop) < 16: continue
        pts = simplify(loop, 1.0)
        parts.append('M' + 'L'.join('%s %s' % (fmt(vb[0] + x / k), fmt(vb[1] + y / k)) for x, y in pts) + 'Z')
    return '<path fill="#fff" d="%s"/>\n' % ''.join(parts)

META = {
    'right': 'looking right (XP "KingBitmap", default)',
    'left': 'looking left (XP "KingLeft", mouse over the free cells)',
    'smile': 'smiling (XP "KingSmile", after a win)',
}

def build(src, variant):
    layers, pip = load(src)
    drop(layers[4], (-0.31, 645.18))                     # the card's dividing line
    face = smile(layers[4], layers[5]) if variant == 'smile' else ''
    defs = '<symbol id="pip" viewBox="-600 -600 1200 1200" preserveAspectRatio="xMinYMid">%s</symbol>' % pip
    body = compose(layers, CROWN_TOP + face, mantle())
    x0, y0, side = CROP
    body = backing(defs, body, (x0 - 40, y0, side + 80, side + 40)) + body   # margin past the crop
    meta = ('<title>FreeCell HD king, %s</title>\n<desc>Derived by res/freecell/king/src/derive_kings.py from the '
            'King of Spades of "SVG playing cards" by Adrian Kennard (https://www.me.uk/cards/, CC0 1.0, '
            'after Goodall &amp; Son). This derivative is also CC0 1.0.</desc>\n' % META[variant])
    return svg_doc(defs, body, (x0, y0, side, side), (OUT_PX, OUT_PX), variant == 'left', meta)

if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    for v in ('right', 'left', 'smile'):
        with open(os.path.join(sys.argv[2], 'king_%s.svg' % v), 'w', encoding='utf-8') as f:
            f.write(build(sys.argv[1], v))
