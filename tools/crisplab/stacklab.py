#!/usr/bin/env python3
"""stacklab — can a buried card be identified from the strip that shows in a stacked column?

Solitaire HD fans face-up tableau cards by step_up = round(15 s) (15/96 of the card height) and FreeCell
HD by 9 ch / 46, so only the top strip of each covered card shows. This tool:

  1. LAYOUT  re-lays-out a RevK card SVG (res/common/cards-svg/<R><S>.svg, viewBox -120 -168 240 336)
             deterministically: rank-index glyph scale/stroke/position, index-suit placement
             (classic = under the rank, split = top-right corner as the generator's 'splitindex',
             side = right of the rank, none), body-pip grid scale (2-10), court picture scale and
             court-pip size, ace pip scale. Applied after tools/edit_card_svg.py's art edit.
  2. GLANCE  renders every card through the crisp runtime pipeline (crisplab's model of
             cardset.c/image.c, candidate 'art+darkbias+sharpen+clamp' = v1.1.1 as shipped), crops the
             strip that a stacked column shows, and measures how far each strip is from the nearest
             strip of a DIFFERENT card (RMS CIE76 dE after a glance blur, linear-light), plus a
             nearest-neighbour recogniser on jittered/noisy/blurred strips.
  3. SHEETS  stacked K..A alternating-colour runs (Solitaire and FreeCell steps), full-card views,
             and blind A-D sheets with an XP row.

usage (from the repo root; CRISPLAB_XP = dir of XP cards_bitmap_<id>.png for the XP rows):
  stacklab.py svg  <in.svg> <out.svg> --layout '<json>' [--art 130,1.6,#223,1.5]   # one card (make_assets)
  stacklab.py export  -v variants.json --only NAME -o dir      # all 52 edited SVGs of a variant
  stacklab.py measure -v variants.json [--only ...]            # ink boxes (fractions of ch) per variant
  stacklab.py glance  -v variants.json -o out [--heights 72,96,128,257]   # metrics.json / metrics.md
  stacklab.py sheets  -v variants.json -o out [--heights 96,257]          # stack + full-card sheets
  stacklab.py blind   -v variants.json -o final --only a,b,c,d --seed N   # A-D + XP, mapping.json

A variants file is a JSON list of {"name", "layout": {...}, optional "svg_dir"} (svg_dir: read the
source SVGs from there instead of res/common/cards-svg, e.g. the generator's own splitindex output).
Layout keys and defaults: LAYOUT_DEFAULTS below ({} = the generator's layout, i.e. CURRENT).
"""
import argparse
import importlib.util
import json
import math
import random
import re
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import crisplab as cl  # noqa: E402

_spec = importlib.util.spec_from_file_location('edit_card_svg', HERE.parent / 'edit_card_svg.py')
edit_card_svg = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(edit_card_svg)

SUITS = 'CDHS'
RANKS = 'A23456789TJQK'
ALL = [r + s for s in SUITS for r in RANKS]
RED = set('DH')
# the art edit and resampler of v1.1.1 (tools/make_assets.sh defaults, crisplab candidates/shipped.json)
ART = (130, 1.6, '#223', 1.5)
PIPE = {"name": "shipped", "space_gamma": 0.6, "tone_ramp": [2.0, 4.0],
        "sharpen": {"mode": "sep", "sigma": 0, "amount": 0.2, "ramp": [2.0, 4.0], "clamp": True},
        "ramp_min": 0.1}
HEIGHTS = [72, 96, 128, 257]
TOP, CH_U = -168.0, 336.0          # SVG card top and height (units)

LAYOUT_DEFAULTS = dict(
    rank_scale=1.0,      # rank glyph box = 50 * rank_scale units (generator: 50)
    rank_stroke=None,    # rank stroke in glyph units (None: as the art edit leaves it, 130)
    rank_bottom=None,    # ink bottom of the rank glyph (the 460 + stroke/2 line) as a fraction of the
                         # card height from the top (None: the generator's box top y = -156)
    rank_cx=-93.0,       # centre x of the rank glyph box (generator: -118 + 25)
    rank_sx=1.0,         # horizontal stretch of the rank glyph about its centre (stems widen with it)
    rank_unclip=False,   # the glyph symbol's viewBox clips strokes at +-500 (with stroke 130 the top and
                         # bottom bars lose 25 units); True widens it to +-600 (same glyph scale)
    suit_mode='classic', # classic (under the rank) | split (top-right) | side (right of the rank) |
                         # both (classic + a top-right corner suit) | none
    suit_scale=1.0,      # index suit box = 41.827 * suit_scale (ink = 5/6 of the box)
    corner_scale=0.86,   # 'both': the extra top-right suit's box = 41.827 * corner_scale
    suit_gap=None,       # classic: rank ink bottom -> suit ink top (None: the generator's box y = -101)
                         # side: rank ink right (K: x 285 + stroke/2) -> suit ink left (default 4)
    suit_dx=0.0,         # extra x offset of the index suit (split: positive = towards the card edge)
    suit_dy=None,        # split/side: suit centre y relative to the rank box centre (None: 0.9135, the
                         # generator's splitindex)
    pip_scale=1.0,       # body pips (2-10, and the A of C/D/H) box = 50 * pip_scale
    pip_top=None,        # ink top of the top pip row as a fraction of ch (None: keep the grid; else the
                         # grid's y spacing is scaled about the centre so the top row lands there)
    pip_ys=1.0,          # used when pip_top is None: y scale of the pip grid about the card centre
    pip_xs=1.0,          # x scale of the pip grid about the card centre
    court_scale=1.0,     # court picture (art, frame, court pip) scaled about the card centre: number
                         # or [sx, sy]
    court_top=None,      # alternative: the frame's top edge as a fraction of ch (sets sy, sx = sy unless
                         # court_sx is given)
    court_sx=None,
    court_pip_scale=1.0, # court pip box 48.4 * (court sy) * court_pip_scale, centre moved with the picture
    court_pip_dy=0.0,    # extra y offset of the court pip (units, negative = up)
    court_pip_top=None,  # alternative: the court pip's ink top as a fraction of ch
    court_frame_top=None,  # extend only the frame rectangle (not the art) to this top edge (fraction of
                           # ch, symmetric at the bottom): room for the court pip above the picture
    ace_scale=1.0,       # the single pip of AC/AD/AH (the AS emblem is left alone)
)

_NUM = r'(-?[0-9.]+(?:e-?[0-9]+)?)'


def _get(tag, a):
    m = re.search(r'\s%s="%s"' % (a, _NUM), tag)
    return float(m.group(1)) if m else None


def _set(tag, **kv):
    for a, v in kv.items():
        s = '%.4f' % v if isinstance(v, float) else str(v)
        s = s.rstrip('0').rstrip('.') if '.' in s else s
        if re.search(r'\s%s="[^"]*"' % a, tag):
            tag = re.sub(r'(\s%s=")[^"]*(")' % a, lambda m: m.group(1) + s + m.group(2), tag, count=1)
        else:
            tag = tag.replace('<use', '<use %s="%s"' % (a, s), 1)
    return tag


def layout_params(layout):
    L = dict(LAYOUT_DEFAULTS)
    for k, v in (layout or {}).items():
        if k not in L:
            raise KeyError('unknown layout parameter %r' % k)
        L[k] = v
    return L


def rank_geometry(L, stroke):
    """(box_x, box_y, box, ink_top, ink_bottom) of the rank glyph: glyph ink y = +-(460 + stroke/2),
    clipped at the symbol's viewBox (+-500, or +-600 with rank_unclip). box is the 1000-unit glyph
    scale (the use box is 1.2x that with rank_unclip)."""
    box = 50.0 * L['rank_scale']
    u = box / 1000.0
    half = min(460 + stroke / 2.0, 600 if L['rank_unclip'] else 500) * u
    if L['rank_bottom'] is None:
        by = -156.0
        cy = by + box / 2
    else:
        cy = TOP + L['rank_bottom'] * CH_U - half
        by = cy - box / 2
    return L['rank_cx'] - box / 2, by, box, cy - half, cy + half


def court_factors(L):
    if L['court_top'] is not None:
        sy = (TOP + L['court_top'] * CH_U) / -116.0
        sx = L['court_sx'] if L['court_sx'] is not None else sy
    else:
        cs = L['court_scale']
        sx, sy = (cs, cs) if not isinstance(cs, (list, tuple)) else cs
        if L['court_sx'] is not None:
            sx = L['court_sx']
    return float(sx), float(sy)


def layout_svg(svg, code, layout):
    """The card's SVG (already art-edited) with the layout applied. {} returns it unchanged."""
    L = layout_params(layout)
    if not layout:
        return svg
    head, body = svg.split('</defs>', 1)
    # rank glyph stroke (the V symbol)
    m = re.search(r'<symbol id="V[^"]*"[^>]*>.*?</symbol>', head, re.S)
    stroke = float(re.search(r'stroke-width="([0-9.]+)"', m.group(0)).group(1))
    sym = m.group(0)
    if L['rank_stroke'] is not None:
        sym = re.sub(r'stroke-width="[0-9.]+"', 'stroke-width="%g"' % L['rank_stroke'], sym)
        stroke = float(L['rank_stroke'])
    if L['rank_unclip']:
        sym = sym.replace('viewBox="-500 -500 1000 1000"', 'viewBox="-600 -600 1200 1200"')
    head = head[:m.start()] + sym + head[m.end():]
    rx, ry, rbox, ink_t, ink_b = rank_geometry(L, stroke)
    rank, is_court, is_ace = code[0], code[0] in 'JQK', code[0] == 'A'
    # pip grid: the top row's centre (min y over the 50-unit pips, top half)
    pips = [(_get(t, 'x'), _get(t, 'y'), _get(t, 'height')) for t in re.findall(r'<use\b[^>]*>', body)
            if re.search(r'href="#S', t) and abs((_get(t, 'height') or 0) - 50) < 1e-6]
    top_cy = min((y + h / 2 for x, y, h in pips), default=0.0)
    ys = L['pip_ys']
    if L['pip_top'] is not None and not is_ace and not is_court and top_cy < -1:
        ink_half = 50.0 * L['pip_scale'] * 500 / 1200
        ys = (TOP + L['pip_top'] * CH_U + ink_half) / top_cy
    sx, sy = court_factors(L)

    def use(mt):
        t = mt.group(0)
        href = re.search(r'href="#([^"]+)"', t).group(1)
        h = _get(t, 'height')
        if href.startswith('V'):                                   # rank index
            if L['rank_unclip']:
                t = _set(t, x=rx - rbox * 0.1, y=ry - rbox * 0.1, width=rbox * 1.2, height=rbox * 1.2)
            else:
                t = _set(t, x=rx, y=ry, width=rbox, height=rbox)
            if L['rank_sx'] != 1.0:
                cx, cy = rx + rbox / 2, ry + rbox / 2
                t = t.replace('<use', '<use transform="translate(%g,%g) scale(%g,1) translate(%g,%g)"' % (
                    cx, cy, L['rank_sx'], -cx, -cy), 1)
            return t
        if href.startswith('S') and h is not None and abs(h - 41.827) < 1e-3:   # index suit
            if L['suit_mode'] == 'none':
                return ''
            sb = 41.827 * L['suit_scale']

            def classic(t):
                cx = L['rank_cx'] + (0 if L['suit_mode'] == 'both' else L['suit_dx'])
                if L['suit_gap'] is None:
                    by = -101.0 if L['suit_scale'] == 1 else -101.0 + (41.827 - sb) / 2
                else:
                    by = ink_b + L['suit_gap'] - sb / 12
                return _set(t, x=cx - sb / 2, y=by, width=sb, height=sb)
            if L['suit_mode'] == 'classic':
                return classic(t)
            dy = 0.9135 if L['suit_dy'] is None else L['suit_dy']
            cy = ry + rbox / 2 + dy
            if L['suit_mode'] == 'both':     # classic index (generator size/place) + a split corner suit
                cs = 41.827 * L['corner_scale']
                cx = -L['rank_cx'] + L['suit_dx']
                return classic(t) + '</use>' + _set(t, x=cx - cs / 2, y=cy - cs / 2, width=cs, height=cs)
            if L['suit_mode'] == 'split':
                cx = -L['rank_cx'] + L['suit_dx']
            elif L['suit_mode'] == 'side':
                gap = 4.0 if L['suit_gap'] is None else L['suit_gap']
                cx = L['rank_cx'] + (285 + stroke / 2) * rbox / 1000 * L['rank_sx'] + gap + sb / 3 + L['suit_dx']
            else:
                raise ValueError(L['suit_mode'])
            return _set(t, x=cx - sb / 2, y=cy - sb / 2, width=sb, height=sb)
        if href.startswith('S') and h is not None and abs(h - 48.4) < 1e-3:     # court pip
            x, y = _get(t, 'x'), _get(t, 'y')
            cx, cy = (x + h / 2) * sx, (y + h / 2) * sy + L['court_pip_dy']
            nb = h * sy * L['court_pip_scale']
            if L['court_pip_top'] is not None:
                cy = TOP + L['court_pip_top'] * CH_U + nb * 5 / 12
            return _set(t, x=cx - nb / 2, y=cy - nb / 2, width=nb, height=nb)
        if href.startswith('S') and h is not None and abs(h - 50) < 1e-6:       # body pip / ace pip
            x, y = _get(t, 'x'), _get(t, 'y')
            cx, cy = x + 25, y + 25
            if is_ace:
                nb = 50 * L['ace_scale']
                return _set(t, x=cx - nb / 2, y=cy - nb / 2, width=nb, height=nb)
            nb = 50 * L['pip_scale']
            cx, cy = cx * L['pip_xs'], cy * ys
            return _set(t, x=cx - nb / 2, y=cy - nb / 2, width=nb, height=nb)
        if is_court and (sx, sy) != (1.0, 1.0) and (re.match(r'[CDHS][JQK]\d', href) or href.startswith('X')):
            tr = re.search(r'\stransform="([^"]*)"', t)
            sc = 'scale(%g,%g)' % (sx, sy)
            if tr:
                t = t[:tr.start(1)] + sc + ' ' + tr.group(1) + t[tr.end(1):]
            else:
                t = t.replace('<use', '<use transform="%s"' % sc, 1)
            if href.startswith('X'):
                w = _get(t, 'stroke-width') or 1.0
                t = _set(t, **{'stroke-width': w / math.sqrt(sx * sy)})
            return t
        return t

    body = re.sub(r'<use\b[^>]*>', use, body)
    if is_court and L['court_frame_top'] is not None:
        ft = TOP + L['court_frame_top'] * CH_U
        head = re.sub(r'<rect id="X[^"]*"[^>]*>', lambda m: _set(m.group(0).replace('<rect', '<use', 1),
                      y=ft, height=-2 * ft).replace('<use', '<rect', 1), head)
    return head + '</defs>' + body


# ---- variants -----------------------------------------------------------------------------------------

class Variant:
    def __init__(self, d):
        self.name = d['name']
        self.label = d.get('label', self.name)
        self.layout = d.get('layout', {})
        self.svg_dir = Path(d['svg_dir']) if d.get('svg_dir') else cl.SVG_DIR
        self.d = d
        self._sprites = {}
        self._inks = {}

    def svg(self, code):
        s = (self.svg_dir / (code + '.svg')).read_text()
        return layout_svg(edit_card_svg.edit(s, *ART), code, self.layout)

    def sprite(self, code, ch):
        """premultiplied RGBA int sprite at height ch (crisp pipeline, v1.1.1)."""
        k = (code, ch)
        if k not in self._sprites:
            cw, ch_ = cl.card_size(ch)
            p = cl.resolve(PIPE, ch)
            m = cl.clean_master(cl.rasterise(self.svg(code), p['master_w'], p['master_h']))
            self._sprites[k] = cl.card_finish(cl.resample(m, cw, ch_, p))
        return self._sprites[k]


class XPVariant:
    name = label = 'XP'

    def sprite(self, code, ch):
        return cl.xp_card(code, ch)


def load_variants(path, only=None):
    vs = [Variant(d) for d in json.loads(Path(path).read_text())]
    if only:
        keep = only.split(',')
        vs = sorted([v for v in vs if v.name in keep], key=lambda v: keep.index(v.name))
    return vs


def steps(ch):
    """(Solitaire face-up step, FreeCell step) at card height ch (layout.c: round(15 s), 9 ch / 46)."""
    return int(math.floor(15 * ch / 96 + 0.5)), 9 * ch // 46


def prerender(variants, heights, cards=ALL, jobs=8):
    jobs_l = [(v, c, h) for v in variants for h in heights for c in cards if isinstance(v, Variant)]
    with ThreadPoolExecutor(jobs) as ex:
        list(ex.map(lambda a: a[0].sprite(a[1], a[2]), jobs_l))


# ---- glance metric -------------------------------------------------------------------------------------

def to_lin(u8):
    return cl.srgb_to_lin(np.asarray(u8, float))


M_XYZ = np.array([[0.4124564, 0.3575761, 0.1804375],
                  [0.2126729, 0.7151522, 0.0721750],
                  [0.0193339, 0.1191920, 0.9503041]])
WHITE = M_XYZ.sum(1)


def lin_to_lab(lin):
    xyz = lin @ M_XYZ.T / WHITE
    f = np.where(xyz > (6 / 29) ** 3, np.cbrt(xyz), xyz / (3 * (6 / 29) ** 2) + 4 / 29)
    return np.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]), 200 * (f[..., 1] - f[..., 2])], -1)


def gauss1d(sigma):
    if sigma <= 0:
        return np.array([1.0])
    r = max(1, int(math.ceil(3 * sigma)))
    x = np.arange(-r, r + 1)
    g = np.exp(-x * x / (2 * sigma * sigma))
    return g / g.sum()


def blur(img, sigma):
    """separable Gaussian, edge-replicated, over axes 0 and 1 of an HxWxC float image."""
    g = gauss1d(sigma)
    r = len(g) // 2
    if r == 0:
        return img
    p = np.pad(img, ((r, r), (0, 0), (0, 0)), mode='edge')
    img = sum(g[k] * p[k:k + img.shape[0]] for k in range(len(g)))
    p = np.pad(img, ((0, 0), (r, r), (0, 0)), mode='edge')
    return sum(g[k] * p[:, k:k + img.shape[1]] for k in range(len(g)))


def shift(img, dx, dy):
    """bilinear sub-pixel shift (edge-replicated)."""
    H, W = img.shape[:2]
    ys = np.clip(np.arange(H) - dy, 0, H - 1)
    xs = np.clip(np.arange(W) - dx, 0, W - 1)
    y0 = np.floor(ys).astype(int)
    x0 = np.floor(xs).astype(int)
    y1 = np.minimum(y0 + 1, H - 1)
    x1 = np.minimum(x0 + 1, W - 1)
    fy = (ys - y0)[:, None, None]
    fx = (xs - x0)[None, :, None]
    a = img[y0][:, x0] * (1 - fx) + img[y0][:, x1] * fx
    b = img[y1][:, x0] * (1 - fx) + img[y1][:, x1] * fx
    return a * (1 - fy) + b * fy


GLANCE_SIGMA = 0.7       # px: display + eye blur of a glance at a normal viewing distance
# recogniser jitter: sub-pixel shift, blur, gain and additive noise (sRGB levels)
JIT = dict(shift=0.5, sigma=(0.5, 1.1), gain=0.06, noise=10.0, n=24)


def strip_lab(rgb_card, step, sigma=GLANCE_SIGMA):
    lin = to_lin(rgb_card[:step])
    return lin_to_lab(blur(lin, sigma))


def card_rgb(v, code, ch):
    return np.clip(cl.on_table(v.sprite(code, ch))[..., :3], 0, 255).astype(np.uint8)


def pairwise(X):
    """RMS dE between all rows (each row = flattened Lab strip)."""
    n_px = X.shape[1] / 3
    sq = (X * X).sum(1)
    d2 = np.maximum(sq[:, None] + sq[None, :] - 2 * X @ X.T, 0) / n_px
    return np.sqrt(d2)


def pair_name(a, b):
    return '%s/%s' % (a, b)


def glance(v, ch, mode, rng_seed=1):
    sol, fc = steps(ch)
    step = sol if mode == 'sol' else fc
    rgbs = {c: card_rgb(v, c, ch) for c in ALL}
    X = np.stack([strip_lab(rgbs[c], step).ravel() for c in ALL])
    D = pairwise(X)
    np.fill_diagonal(D, np.inf)
    nn = D.min(1)
    res = {'min': float(nn.min()), 'p5': float(np.percentile(nn, 5)), 'median': float(np.median(nn))}
    vis = {c: visibility(inks_of(v, c), step / ch) for c in ALL}
    r = min((vis[c][0], c) for c in ALL)
    su = min((vis[c][1], c) for c in ALL)
    res.update(rank_vis=r[0], rank_vis_card=r[1], suit_vis=su[0], suit_vis_card=su[1],
               no_suit=int(sum(vis[c][1] < 0.25 for c in ALL)))
    # suit pairs: same rank, same colour (H/D, S/C)
    sp = []
    for r in RANKS:
        for a, b in (('H', 'D'), ('S', 'C')):
            sp.append((D[ALL.index(r + a), ALL.index(r + b)], r + a, r + b))
    sp.sort()
    res['suit_min'] = float(sp[0][0])
    res['suit_med'] = float(np.median([s[0] for s in sp]))
    res['suit_hard'] = ['%s/%s %.1f' % (a, b, d) for d, a, b in sp[:3]]
    # same suit, different rank
    rp = []
    for s in SUITS:
        for i, r1 in enumerate(RANKS):
            for r2 in RANKS[i + 1:]:
                rp.append((D[ALL.index(r1 + s), ALL.index(r2 + s)], r1 + s, r2 + s))
    rp.sort()
    res['rank_min'] = float(rp[0][0])
    res['rank_hard'] = ['%s/%s %.1f' % (a, b, d) for d, a, b in rp[:3]]
    allp = sorted((D[i, j], ALL[i], ALL[j]) for i in range(52) for j in range(i + 1, 52))
    res['hard'] = ['%s/%s %.1f' % (a, b, d) for d, a, b in allp[:5]]
    # recogniser: nearest template (clean strip, glance blur) for jittered / noisy / blurred strips
    rng = np.random.default_rng(rng_seed)
    ok = suit_err = rank_err = n = 0
    conf = {}
    sq = (X * X).sum(1)
    for i, c in enumerate(ALL):
        lin = to_lin(rgbs[c])
        for _ in range(JIT['n']):
            dx, dy = rng.uniform(-JIT['shift'], JIT['shift'], 2)
            img = shift(lin, dx, dy)[:step]
            img = blur(img, rng.uniform(*JIT['sigma'])) * (1 + rng.uniform(-JIT['gain'], JIT['gain']))
            u8 = cl.lin_to_srgb(img) + rng.normal(0, JIT['noise'], img.shape)
            x = lin_to_lab(to_lin(np.clip(u8, 0, 255))).ravel()
            j = int(np.argmin(sq - 2 * X @ x))
            n += 1
            if j == i:
                ok += 1
            else:
                g = ALL[j]
                if g[0] == c[0]:
                    suit_err += 1
                else:
                    rank_err += 1
                conf[pair_name(c, g)] = conf.get(pair_name(c, g), 0) + 1
    res['acc'] = ok / n
    res['suit_err'] = suit_err / n
    res['rank_err'] = rank_err / n
    res['confusions'] = sorted(conf.items(), key=lambda kv: -kv[1])[:4]
    return res


def run_glance(variants, heights, out, jobs=8):
    out.mkdir(parents=True, exist_ok=True)
    allv = ([XPVariant()] if cl.have_xp() else []) + variants
    prerender(variants, heights, jobs=jobs)
    tasks = [(v, h, m) for v in allv for h in heights for m in ('sol', 'fc')]
    with ThreadPoolExecutor(jobs) as ex:
        res = list(ex.map(lambda a: glance(*a), tasks))
    rows = []
    for (v, h, m), r in zip(tasks, res):
        r.update(name=v.name, h=h, mode=m, step=steps(h)[0 if m == 'sol' else 1])
        rows.append(r)
    (out / 'metrics.json').write_text(json.dumps(rows, indent=1))
    md = ['# glance metrics', '',
          'RMS dE (CIE76, linear-light Gaussian blur sigma %.1f px) between the visible strips; nn = '
          'distance to the nearest strip of a different card (min / 5th pct over 52); suit = same rank, '
          'same colour, other suit (H/D, S/C: min / median of 26 pairs); rank = same suit, other rank '
          '(min); acc = nearest-template recogniser on %d jittered strips per card (shift +-%.1f px, '
          'blur %.1f-%.1f px, gain +-%d%%, noise %g levels); suit_err / rank_err = share of samples '
          'misread as the same rank other suit / another rank. rank vis = visible share of the rank glyph '
          'ink height (min over 52, card); suit vis = visible share of the best suit-shaped element (index '
          'suit, corner suit, top pip, court pip; min over 52, card); no-suit = cards whose best suit cue is '
          'under 25%% visible (XP: read off the bitmaps; its ace strips differ only by a 1-px glyph shift).' % (
              GLANCE_SIGMA, JIT['n'], JIT['shift'], JIT['sigma'][0], JIT['sigma'][1], JIT['gain'] * 100,
              JIT['noise']), '']
    for m in ('sol', 'fc'):
        md += ['## %s strip' % ('Solitaire' if m == 'sol' else 'FreeCell'), '',
               '| variant | h | step | rank vis | suit vis | no-suit | nn min | nn p5 | suit min | suit med | '
               'rank min | acc | suit_err | rank_err | hardest pairs |', '|---|' + '---|' * 14]
        for r in rows:
            if r['mode'] != m:
                continue
            md.append('| %s | %d | %d | %.2f %s | %.2f %s | %d | %.1f | %.1f | %.1f | %.1f | %.1f | %.1f%% | %.1f%% | '
                      '%.1f%% | %s |' % (
                r['name'], r['h'], r['step'], r['rank_vis'], r['rank_vis_card'], r['suit_vis'], r['suit_vis_card'],
                r['no_suit'], r['min'], r['p5'], r['suit_min'], r['suit_med'], r['rank_min'],
                100 * r['acc'], 100 * r['suit_err'], 100 * r['rank_err'], ', '.join(r['hard'][:3])))
        md.append('')
    (out / 'metrics.md').write_text('\n'.join(md))
    return rows


# ---- sheets --------------------------------------------------------------------------------------------

def runs():
    """four K..A alternating-colour runs (going up from the ace the suits cycle S D C H)."""
    cyc = 'SDCH'
    cols = []
    for a in 'SHDC':
        k = cyc.index(a)
        col = [RANKS[r] + cyc[(k + r) % 4] for r in range(13)]
        cols.append(col[::-1])           # K first (buried) ... A last (on top)
    return cols


def column_rgb(v, cards, ch, step):
    sprites = [v.sprite(c, ch) for c in cards]
    cw = sprites[0].shape[1]
    H = ch + step * (len(cards) - 1)
    canvas = np.zeros((H, cw, 4), np.int64)
    canvas[..., :3] = cl.TABLE
    canvas[..., 3] = 255
    for i, s in enumerate(sprites):
        y = i * step
        canvas[y:y + ch] = cl.over(s, canvas[y:y + ch])
    return cl.to_rgb8(canvas)


def stack_sheet(variants, ch, mode, zoom=1, label_w=None, font_px=14, title=None):
    sol, fc = steps(ch)
    step = sol if mode == 'sol' else fc
    rows = [(v.label, [column_rgb(v, col, ch, step) for col in runs()]) for v in variants]
    return cl.compose(rows, ch, zoom, title, label_w=label_w, font_px=font_px)


FULL = ['AS', '7H', 'TC', 'JD', 'QS', 'KH']


def full_sheet(variants, ch, zoom=1, label_w=None, font_px=14, title=None, cards=FULL):
    rows = [(v.label, [cl.to_rgb8(cl.on_table(v.sprite(c, ch))) for c in cards]) for v in variants]
    return cl.compose(rows, ch, zoom, title, label_w=label_w, font_px=font_px)


def write_sheets(variants, heights, out, jobs=8, zoom_max=128):
    out.mkdir(parents=True, exist_ok=True)
    allv = ([XPVariant()] if cl.have_xp() else []) + variants
    prerender(variants, heights, jobs=jobs)
    paths = []
    for ch in heights:
        for mode in ('sol', 'fc'):
            t = '%s step %d at h=%d' % ('Solitaire' if mode == 'sol' else 'FreeCell',
                                        steps(ch)[0 if mode == 'sol' else 1], ch)
            for z in ((1, 2) if ch <= zoom_max else (1,)):
                p = out / ('stack_%s_%d%s.png' % (mode, ch, '_x2' if z == 2 else ''))
                stack_sheet(allv, ch, mode, z, title=t).save(p, optimize=True)
                paths.append(p)
        for z in ((1, 2) if ch <= zoom_max else (1,)):
            p = out / ('fullcards_%d%s.png' % (ch, '_x2' if z == 2 else ''))
            full_sheet(allv, ch, z, title='h=%d' % ch).save(p, optimize=True)
            paths.append(p)
    return paths


# ---- measure ------------------------------------------------------------------------------------------

def element_inks(v, code, H=1120):
    """[(kind, y0, y1)] ink extents (fractions of the card height) of the top-half rank glyph and of each
    suit-shaped element (index suit, body pips, court pip; not the AS emblem), rendered one row at a time."""
    if (code, H) in v._inks:
        return v._inks[(code, H)]
    svg = v.svg(code)
    head, body = svg.split('</defs>', 1)
    top_body = body.split('<g transform="rotate(180)">')[0]
    W = int(round(H * 240 / 336))
    groups = {}
    for t in re.findall(r'<use\b[^>]*>', top_body):
        h = _get(t, 'height')
        if re.search(r'href="#V', t):
            groups.setdefault(('rank', 0), []).append(t)
        elif re.search(r'href="#S', t) and h is not None and h < 100:
            groups.setdefault(('suit', round(_get(t, 'y'), 3)), []).append(t)
    out = []
    for (kind, _), tags in sorted(groups.items()):
        s = head + '</defs>' + ''.join(t + '</use>' for t in tags) + '</svg>'
        a = cl.rasterise(s, W, H)[..., 3]
        rows = np.nonzero((a > 127).any(1))[0]
        if len(rows):
            out.append((kind, rows.min() / H, (rows.max() + 1) / H))
    v._inks[(code, H)] = out
    return out


def xp_inks(code):
    """the same for the XP bitmap (71x96), read off the bitmaps: rank rows 4-15 (Q: 4-17, its tail);
    index suit = the next run of ink rows in the index column (x 1-11); top body pip (2-10) rows 10-23;
    court pip inside the picture frame rows 13-23."""
    a = cl.xp_card(code, 96)[..., :3]
    ink = (a.max(2) - a.min(2) > 60) | (a.max(2) < 100)
    col = ink[:, 1:12].any(1)
    r1 = 18 if code[0] == 'Q' else 16
    y0 = r1 + int(np.argmax(col[r1:]))
    y1 = y0 + int(np.argmin(col[y0:]))
    out = [('rank', 4 / 96, r1 / 96), ('suit', y0 / 96, y1 / 96)]
    if code[0] in 'JQK':
        out.append(('suit', 13 / 96, 24 / 96))
    elif code[0] != 'A':
        out.append(('suit', 10 / 96, 24 / 96))
    return out


def inks_of(v, code):
    return xp_inks(code) if isinstance(v, XPVariant) else element_inks(v, code)


def visibility(inks, cut):
    """(rank visible fraction, best suit-cue visible fraction) for a strip ending at 'cut' (fraction)."""
    f = lambda y0, y1: min(1.0, max(0.0, (cut - y0) / (y1 - y0)))
    rank = min(f(y0, y1) for k, y0, y1 in inks if k == 'rank')
    suit = max([f(y0, y1) for k, y0, y1 in inks if k == 'suit'] or [0.0])
    return rank, suit


# ---- CLI ----------------------------------------------------------------------------------------------

def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('cmd', choices=['svg', 'export', 'measure', 'glance', 'sheets', 'blind'])
    ap.add_argument('args', nargs='*')
    ap.add_argument('--variants', '-v')
    ap.add_argument('--only')
    ap.add_argument('--out', '-o')
    ap.add_argument('--heights')
    ap.add_argument('--layout', default='{}')
    ap.add_argument('--art', default=','.join(map(str, ART)), help='index_stroke,court_mult,colour,frame_w')
    ap.add_argument('--xp', default=None)
    ap.add_argument('--cache', default=None)
    ap.add_argument('--seed', type=int, default=None)
    ap.add_argument('--jobs', type=int, default=8)
    a = ap.parse_args(argv)
    if a.xp:
        cl.XP_DIR = Path(a.xp)
    if a.cache:
        cl.CACHE = Path(a.cache)
    if a.cmd == 'svg':
        src, dst = a.args
        art = a.art.split(',')
        s = Path(src).read_text()
        code = re.search(r'face="([^"]+)"', s).group(1)
        s = layout_svg(edit_card_svg.edit(s, *art), code, json.loads(a.layout))
        Path(dst).write_text(s)
        return 0
    vs = load_variants(a.variants, a.only)
    out = Path(a.out) if a.out else None
    if a.cmd == 'export':
        for v in vs:
            d = out / v.name if len(vs) > 1 else out
            d.mkdir(parents=True, exist_ok=True)
            for c in ALL:
                (d / (c + '.svg')).write_text(v.svg(c))
            print(d)
    elif a.cmd == 'measure':
        for v in ([XPVariant()] if cl.have_xp() else []) + vs:
            inks = {c: inks_of(v, c) for c in ALL}
            rb = max((y1, c) for c in ALL for k, y0, y1 in inks[c] if k == 'rank')
            rt = min((y0, c) for c in ALL for k, y0, y1 in inks[c] if k == 'rank')
            line = '%-14s rank ink y %.4f(%s)..%.4f(%s)' % (v.name, rt[0], rt[1], rb[0], rb[1])
            for lbl, cut in (('sol', 15 / 96), ('fc', 18 / 96)):
                vis = {c: visibility(inks[c], cut) for c in ALL}
                r = min((vis[c][0], c) for c in ALL)
                su = min((vis[c][1], c) for c in ALL)
                line += ' | %s: rank_vis min %.2f(%s) suit_vis min %.2f(%s) no-suit %d' % (
                    lbl, r[0], r[1], su[0], su[1], sum(vis[c][1] < 0.25 for c in ALL))
            print(line)
    elif a.cmd == 'glance':
        hs = [int(h) for h in (a.heights or ','.join(map(str, HEIGHTS))).split(',')]
        run_glance(vs, hs, out, a.jobs)
        print(out / 'metrics.md')
    elif a.cmd == 'sheets':
        hs = [int(h) for h in (a.heights or '96,257').split(',')]
        for p in write_sheets(vs, hs, out, a.jobs):
            print(p)
    elif a.cmd == 'blind':
        rnd = random.Random(a.seed)
        order = list(range(len(vs)))
        rnd.shuffle(order)
        letters = 'ABCDEFGH'
        mapping = {}
        blind = []
        for i, k in enumerate(order):
            vs[k].label = letters[i]
            mapping[letters[i]] = {'name': vs[k].name, 'layout': vs[k].layout, 'svg_dir': str(vs[k].svg_dir)}
            blind.append(vs[k])
        out.mkdir(parents=True, exist_ok=True)
        (out / 'mapping.json').write_text(json.dumps(mapping, indent=2))
        allv = ([XPVariant()] if cl.have_xp() else []) + blind
        prerender(blind, [96, 257], jobs=a.jobs)
        lw = 44
        stack_sheet(allv, 96, 'sol', 2, lw, 16, 'Solitaire step 15 at h=96, x2').save(out / 'stack_sol_96_x2.png', optimize=True)
        stack_sheet(allv, 96, 'fc', 2, lw, 16, 'FreeCell step 18 at h=96, x2').save(out / 'stack_fc_96_x2.png', optimize=True)
        stack_sheet(allv, 257, 'sol', 1, lw, 16, 'Solitaire step 40 at h=257').save(out / 'stack_sol_257.png', optimize=True)
        full_sheet(allv, 257, 1, lw, 16, 'h=257').save(out / 'fullcards_257.png', optimize=True)
        print(out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
