#!/usr/bin/env python3
"""crisplab — FreeCell HD card-crispness test bench.

Reproduces the runtime card pipeline (src/engine/cardset.c + src/engine/image.c) in numpy for a set of
CANDIDATES (dicts of parameters) and writes comparison sheets, metrics and blind A/B sheets.

Pipeline per card and target height ch (cw = round(71 * ch / 96), as src/freecell/layout.c):
  1. art      res/common/cards-svg/<R><S>.svg, edited: rank-index stroke 80 -> index_stroke (as
              tools/make_assets.sh), court linework (#44F strokes inside the 1300x2000 court art)
              x court_mult and recoloured, optional court frame width/colour, optional colour map
  2. raster   rsvg-convert at master_w x master_h (default 400x560 = res/common/cards/*.png), or at
              cw x ch directly (source='vector': the 'vector at target size' reference)
  3. clean    clean_master(): premultiply, composite over white, whiten the outline band
  4. resample separable filter, independent x/y factors, kernel stretched by the downscale
              factor, window truncated at the image edge and renormalised; optional sharpening
              folded into the separable kernel ('sep') or a 2D unsharp mask after it ('usm2d')
  5. finish   fc_image_card_finish(): analytic rounded rect (r = 0.0372 ch), black frame ring
              (1 px up to ch 300, then ch/300 px)
  6. table    composited over RGB(0,127,0)

Usage: see README.md in this directory.
"""
import argparse
import copy
import csv
import hashlib
import json
import math
import os
import random
import re
import subprocess
import sys
import threading
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
SVG_DIR = REPO / 'res' / 'common' / 'cards-svg'
TABLE = (0, 127, 0)
SUITS = 'CDHS'
RANKS = 'A23456789TJQK'
CARDS = ['AS', 'TC', '8H', 'JD', 'QS', 'KH']          # sheet columns
COLUMN = ['KS', 'QH', 'JC', 'TD', '9S']               # FreeCell column, top (buried) to bottom
COURTS = ['JD', 'QS', 'KH']
HEIGHTS = [72, 84, 96, 112, 128, 160, 200, 257, 300]

CACHE = Path(os.environ.get('CRISPLAB_CACHE', '/tmp/crisplab-cache'))
XP_DIR = Path(os.environ.get('CRISPLAB_XP', '.'))      # cards_bitmap_<id>.png, 71x96


def have_xp():
    """The XP reference bitmaps are there (Path('') is '.', so test for a file, not the dir)."""
    return (XP_DIR / 'cards_bitmap_1.png').exists()

DEFAULTS = dict(
    name='current',
    index_stroke=115,        # rank-index stroke width (generator: 80; shipped: 115)
    court_mult=1.0,          # multiplier on the court-art linework stroke widths (#44F strokes)
    court_colour=None,       # new colour of that linework (None: keep #44F)
    court_fill=None,         # new colour of the court art's #44F fills (None: keep)
    court_frame_w=1.0,       # stroke width (SVG units) of the frame around the court picture
    court_frame_colour=None, # its colour (None: keep #44F)
    colour_map=None,         # {"#FC4": "#FD2", ...}: extra fill/stroke remaps inside court cards
    source='master',         # 'master' (raster at master size, resample) | 'vector' (raster at target)
    master_h=560,
    master_w=None,           # None: round(master_h * 240 / 336) = 400 for 560
    filter='box',            # box|triangle|hermite|mitchell|catrom|lanczos2|lanczos3|lanczos4|magic
    blur=1.0,                # kernel width scale (< 1 sharper / more aliasing)
    linear=False,            # resample in linear light instead of sRGB
    space_gamma=1.0,         # resample in (v/255)^g space (g < 1: dark-biased averaging, 'stem darkening')
    tone_gamma=1.0,          # per-channel LUT v' = 255 (v/255)^g after resampling (g > 1 darkens AA greys)
    tone_ramp=None,          # [f0, f1]: space/tone gamma exponent ramps in from 1 at factor f0 to g at f1
    sharpen=None,            # {"mode": "sep"|"usm2d", "sigma": s, "amount": a, "ramp": [f0, f1],
                             #  "clamp": true (sep, sigma 0: clamp each sample to its 3 taps' range)}
    ramp_min=0.0,            # ramp values t below this count as 0 (the runtime's t < 0.1 cut)
    by_height=None,          # [[max_h, {overrides}], ...]: first entry with ch <= max_h is merged in
)


# ---- art -----------------------------------------------------------------------------------------

def _scale_attr(tag, attr, mult):
    m = re.search(r'\s%s="([0-9.]+)"' % attr, tag)
    if not m:
        return tag
    v = float(m.group(1)) * mult
    return tag[:m.start(1)] + ('%.4g' % v) + tag[m.end(1):]


def _set_attr(tag, attr, value):
    if re.search(r'\s%s="[^"]*"' % attr, tag):
        return re.sub(r'(\s%s=")[^"]*(")' % attr, lambda m: m.group(1) + value + m.group(2), tag, count=1)
    return tag.replace('<' + tag[1:].split()[0], '<' + tag[1:].split()[0] + ' %s="%s"' % (attr, value), 1)


def prep_svg(code, p):
    """The card's SVG with the candidate's art edits applied."""
    s = (SVG_DIR / (code + '.svg')).read_text()
    if p['index_stroke'] != 80:
        s = s.replace('stroke-width="80"', 'stroke-width="%g"' % p['index_stroke'])
    if code[0] not in 'JQK':
        return s

    def edit(m):
        tag = m.group(0)
        if 'stroke="#44F"' in tag:
            if re.search(r'xlink:href="#X', tag):           # the frame around the court picture
                if p['court_frame_w'] != 1.0:
                    tag = _set_attr(tag, 'stroke-width', '%g' % p['court_frame_w'])
                if p['court_frame_colour']:
                    tag = tag.replace('stroke="#44F"', 'stroke="%s"' % p['court_frame_colour'])
            else:                                            # court-art linework
                if p['court_mult'] != 1.0:
                    tag = _scale_attr(tag, 'stroke-width', p['court_mult'])
                if p['court_colour']:
                    tag = tag.replace('stroke="#44F"', 'stroke="%s"' % p['court_colour'])
        if p['court_fill'] and 'fill="#44F"' in tag:
            tag = tag.replace('fill="#44F"', 'fill="%s"' % p['court_fill'])
        for a, b in (p['colour_map'] or {}).items():
            tag = tag.replace('fill="%s"' % a, 'fill="%s"' % b).replace('stroke="%s"' % a, 'stroke="%s"' % b)
        return tag

    return re.sub(r'<(?:path|use)\b[^>]*>', edit, s)


def rasterise(svg_text, w, h):
    """rsvg-convert to straight-alpha RGBA uint8 (cached)."""
    CACHE.mkdir(parents=True, exist_ok=True)
    key = hashlib.sha1(('%d %d\n' % (w, h) + svg_text).encode()).hexdigest()
    png = CACHE / (key + '.png')
    if not png.exists():
        uniq = '%s.%d.%d' % (key, os.getpid(), threading.get_ident())
        tmp_svg = CACHE / (uniq + '.svg')
        tmp_svg.write_text(svg_text)
        tmp_png = CACHE / (uniq + '.tmp.png')
        subprocess.run(['rsvg-convert', '-w', str(w), '-h', str(h), str(tmp_svg), '-o', str(tmp_png)],
                       check=True)
        os.replace(tmp_png, png)
        tmp_svg.unlink()
    a = np.asarray(Image.open(png).convert('RGBA'))
    assert a.shape[:2] == (h, w), (a.shape, w, h)
    return a


# ---- runtime ports --------------------------------------------------------------------------------

def mul255(x, a):
    """x * a / 255 rounded, as image.c mul255 (int arrays)."""
    t = np.asarray(x, np.int64) * a + 128
    return (t + (t >> 8)) >> 8


def clean_master(rgba):
    """cardset.c clean_master(): premultiply (decode), src-over white, whiten the outline band.
    Returns an opaque RGB int array."""
    h, w = rgba.shape[:2]
    c = rgba[..., :3].astype(np.int64)
    a = rgba[..., 3:4].astype(np.int64)
    pm = np.where(a == 255, c, mul255(c, a))
    out = pm + (255 - a)
    band = 3.5 * h / 560.0
    r = 0.0372 * h
    ri = r - band
    cy = np.arange(h)[:, None] + 0.5
    cx = np.arange(w)[None, :] + 0.5
    dy = np.where(cy < r, r - cy, np.where(cy > h - r, cy - (h - r), 0))
    dx = np.where(cx < r, r - cx, np.where(cx > w - r, cx - (w - r), 0))
    edge = (cx < band) | (cy < band) | (cx > w - band) | (cy > h - band) | \
           ((dx > 0) & (dy > 0) & (dx * dx + dy * dy > ri * ri))
    out[edge] = 255
    return out


def _overlap1(a0, a1, b0, b1):
    return np.clip(np.minimum(a1, b1) - np.maximum(a0, b0), 0, None)


def round_rect_coverage(x, y, w, h, r, W, H):
    """image.c fc_round_rect_coverage over a W x H pixel grid."""
    if w <= 0 or h <= 0:
        return np.zeros((H, W))
    r = min(r, w / 2, h / 2)
    r = max(r, 0)
    py = np.arange(H)[:, None].astype(float)
    px = np.arange(W)[None, :].astype(float)
    ox = _overlap1(px, px + 1, x, x + w)
    oy = _overlap1(py, py + 1, y, y + h)
    cx, cy = px + 0.5, py + 0.5
    dx = np.where(cx < x + r, x + r - cx, np.where(cx > x + w - r, cx - (x + w - r), 0))
    dy = np.where(cy < y + r, y + r - cy, np.where(cy > y + h - r, cy - (y + h - r), 0))
    d2 = dx * dx + dy * dy
    res = ox * oy
    straight = (dx == 0) | (dy == 0)
    inside = (r > 0.7072) & (d2 <= (r - 0.7072) ** 2)
    outside = d2 >= (r + 0.7072) ** 2
    amb = ~straight & ~inside & ~outside & (ox > 0) & (oy > 0)
    res = np.where(~straight & outside, 0.0, res)
    res = np.where((ox <= 0) | (oy <= 0), 0.0, res)
    if amb.any():
        ys, xs = np.nonzero(amb)
        cnt = np.zeros(len(ys))
        for j in range(8):
            sy = ys + (j + 0.5) / 8
            for i in range(8):
                sx = xs + (i + 0.5) / 8
                ok = (sx >= x) & (sx < x + w) & (sy >= y) & (sy < y + h)
                ddx = np.where(sx < x + r, x + r - sx, np.where(sx > x + w - r, sx - (x + w - r), 0))
                ddy = np.where(sy < y + r, y + r - sy, np.where(sy > y + h - r, sy - (y + h - r), 0))
                cnt += ok & (ddx * ddx + ddy * ddy <= r * r)
        res[ys, xs] = cnt / 64.0
    return res


def frame_px(ch):
    return 1.0 if ch <= 300 else ch / 300.0


def card_finish(rgb):
    """image.c fc_image_card_finish (black frame, white under). rgb: opaque int HxWx3.
    Returns premultiplied RGBA int."""
    h, w = rgb.shape[:2]
    r = min(0.0372 * h, w / 2, h / 2)
    t = frame_px(h)
    ri = r - t if r - t > 0 else 0
    band = r + 1
    py = np.arange(h)[:, None]
    px = np.arange(w)[None, :]
    ey = (py < t + 1) | (py + 1 > h - t - 1)
    cyb = (py < band) | (py + 1 > h - band)
    edge = ey | (px < t + 1) | (px + 1 > w - t - 1) | (cyb & ((px < band) | (px + 1 > w - band)))
    co = round_rect_coverage(0, 0, w, h, r, w, h)
    ci = round_rect_coverage(t, t, w - 2 * t, h - 2 * t, ri, w, h)
    ring = np.floor((co - ci) * 255 + 0.5).astype(np.int64)
    inn = np.floor(ci * 255 + 0.5).astype(np.int64)
    ring = np.where(ring + inn > 255, 255 - inn, ring)
    out = np.empty((h, w, 4), np.int64)
    out[..., :3] = rgb
    out[..., 3] = 255
    e_rgb = np.minimum(mul255(rgb, inn[..., None]), 255)
    e_a = np.minimum(ring + inn, 255)
    e_rgb = np.minimum(e_rgb, e_a[..., None])
    out[..., :3] = np.where(edge[..., None], e_rgb, rgb)
    out[..., 3] = np.where(edge, e_a, 255)
    return out


def over(src, dst):
    """premultiplied src over opaque/premultiplied dst (image.c over())."""
    inv = 255 - src[..., 3:4]
    return src + mul255(dst, inv)


def on_table(card):
    bg = np.empty_like(card)
    bg[..., :3] = TABLE
    bg[..., 3] = 255
    return over(card, bg)


# ---- filters ---------------------------------------------------------------------------------------

def _sinc(x):
    return np.sinc(x)


def _cubic(B, C):
    def k(x):
        x = np.abs(x)
        return np.where(x < 1, ((12 - 9 * B - 6 * C) * x ** 3 + (-18 + 12 * B + 6 * C) * x ** 2 + (6 - 2 * B)) / 6,
                        np.where(x < 2, ((-B - 6 * C) * x ** 3 + (6 * B + 30 * C) * x ** 2 +
                                         (-12 * B - 48 * C) * x + (8 * B + 24 * C)) / 6, 0))
    return k


KERNELS = {
    'triangle': (1.0, lambda x: np.clip(1 - np.abs(x), 0, None)),
    'hermite': (1.0, lambda x: np.where(np.abs(x) < 1, 2 * np.abs(x) ** 3 - 3 * x * x + 1, 0)),
    'mitchell': (2.0, _cubic(1 / 3, 1 / 3)),
    'catrom': (2.0, _cubic(0, 0.5)),
    'lanczos2': (2.0, lambda x: np.where(np.abs(x) < 2, _sinc(x) * _sinc(x / 2), 0)),
    'lanczos3': (3.0, lambda x: np.where(np.abs(x) < 3, _sinc(x) * _sinc(x / 3), 0)),
    'lanczos4': (4.0, lambda x: np.where(np.abs(x) < 4, _sinc(x) * _sinc(x / 4), 0)),
    # Costella's magic kernel (quadratic B-spline); 'magic sharp' = this + sep sharpen a=0.25 sigma=0
    'magic': (1.5, lambda x: np.where(np.abs(x) <= 0.5, 0.75 - x * x,
                                      np.where(np.abs(x) <= 1.5, 0.5 * (np.abs(x) - 1.5) ** 2, 0))),
}


def box_weights(src_n, dst_n):
    """Exact area weights, as image.c taps_build (box)."""
    W = np.zeros((dst_n, src_n))
    for i in range(dst_n):
        f0, f1 = i * src_n, (i + 1) * src_n
        j0, j1 = f0 // dst_n, (f1 - 1) // dst_n
        for j in range(j0, j1 + 1):
            a = max(f0, j * dst_n)
            b = min(f1, (j + 1) * dst_n)
            W[i, j] = (b - a) / src_n
    return W


def kernel_weights(src_n, dst_n, name, blur=1.0):
    if name == 'box':
        return box_weights(src_n, dst_n)
    support, k = KERNELS[name]
    scale = src_n / dst_n
    fs = max(scale, 1.0) * blur
    W = np.zeros((dst_n, src_n))
    for i in range(dst_n):
        c = (i + 0.5) * scale
        j0 = max(0, int(math.floor(c - support * fs)))
        j1 = min(src_n - 1, int(math.ceil(c + support * fs)))
        j = np.arange(j0, j1 + 1)
        wt = k((j + 0.5 - c) / fs)
        W[i, j0:j1 + 1] = wt / wt.sum()
    return W


def gauss_taps(sigma):
    """normalised discrete Gaussian; sigma 0 -> [0.5? no] the 3-tap [1,2,1]/4 (binomial)."""
    if sigma <= 0:
        return np.array([0.25, 0.5, 0.25])
    rad = max(1, int(math.ceil(2.5 * sigma)))
    x = np.arange(-rad, rad + 1)
    g = np.exp(-x * x / (2 * sigma * sigma))
    return g / g.sum()


def ramp_value(f, f0, f1, ramp_min=0.0):
    t = min(1.0, max(0.0, (f - f0) / (f1 - f0)))
    return 0.0 if t < ramp_min else t


def sharpen_amount(sh, f, ramp_min=0.0):
    a = sh['amount']
    if sh.get('ramp'):
        a *= ramp_value(f, sh['ramp'][0], sh['ramp'][1], ramp_min)
    return a


def sharpen3_clamped(img, a, axis):
    """[-a/4, 1 + a/2, -a/4] along axis (clamp-to-edge), each result clamped to the [min, max] of the
    three samples it was computed from: image.c sharpen3 (the overshoot clamp, no halos)."""
    n = img.shape[axis]
    i = np.arange(n)
    lft = np.take(img, np.maximum(i - 1, 0), axis=axis)
    rgt = np.take(img, np.minimum(i + 1, n - 1), axis=axis)
    v = (1 + a / 2) * img - a / 4 * (lft + rgt)
    lo = np.minimum(np.minimum(lft, rgt), img)
    hi = np.maximum(np.maximum(lft, rgt), img)
    return np.clip(v, lo, hi)


def sharpen_matrix(n, g, a):
    """1D (1 + a) delta - a g at output scale, clamp-to-edge, as an n x n matrix."""
    S = np.eye(n) * (1 + a)
    rad = len(g) // 2
    for i in range(n):
        for k, gv in enumerate(g):
            j = min(n - 1, max(0, i + k - rad))
            S[i, j] -= a * gv
    return S


def srgb_to_lin(v):
    v = v / 255.0
    return np.where(v <= 0.04045, v / 12.92, ((v + 0.055) / 1.055) ** 2.4)


def lin_to_srgb(v):
    v = np.clip(v, 0, 1)
    return 255.0 * np.where(v <= 0.0031308, v * 12.92, 1.055 * v ** (1 / 2.4) - 0.055)


def resample(rgb, cw, ch, p):
    """opaque master rgb (int HxWx3) -> opaque cw x ch rgb ints, per the candidate."""
    mh, mw = rgb.shape[:2]
    Wy = kernel_weights(mh, ch, p['filter'], p['blur'])
    Wx = kernel_weights(mw, cw, p['filter'], p['blur'])
    sh = p['sharpen']
    f = mh / ch
    a_post = 0.0          # clamped sharpen: separate x then y passes after the box (not foldable)
    if sh and sh.get('mode', 'sep') == 'sep':
        a = sharpen_amount(sh, f, p['ramp_min'])
        if a and sh.get('clamp'):
            assert sh.get('sigma', 0) == 0, 'clamp needs the 3-tap sharpen (sigma 0)'
            a_post = a
        elif a:
            g = gauss_taps(sh.get('sigma', 0))
            Wy = sharpen_matrix(ch, g, a) @ Wy
            Wx = sharpen_matrix(cw, g, a) @ Wx
    src = rgb.astype(float)
    ramp = 1.0
    if p['tone_ramp']:
        ramp = ramp_value(f, p['tone_ramp'][0], p['tone_ramp'][1], p['ramp_min'])
    sg = 1.0 + (p['space_gamma'] - 1.0) * ramp
    tg = 1.0 + (p['tone_gamma'] - 1.0) * ramp
    if p['linear']:
        src = srgb_to_lin(src)
    elif sg != 1.0:
        src = (src / 255.0) ** sg
    out = sep_apply(Wy, Wx, src)
    if a_post:
        out = sharpen3_clamped(sharpen3_clamped(out, a_post, 1), a_post, 0)
    if p['linear']:
        out = lin_to_srgb(out)
    elif sg != 1.0:
        out = 255.0 * np.clip(out, 0, 1) ** (1.0 / sg)
    if tg != 1.0:
        out = 255.0 * (np.clip(out, 0, 255) / 255.0) ** tg
    out = np.clip(np.floor(out + 0.5), 0, 255)
    if sh and sh.get('mode') == 'usm2d':
        a = sharpen_amount(sh, f, p['ramp_min'])
        if a:
            g = gauss_taps(sh.get('sigma', 0.6))
            Gy = _blur_matrix(ch, g)
            Gx = _blur_matrix(cw, g)
            bl = sep_apply(Gy, Gx, out)
            thr = sh.get('threshold', 0)
            d = out - bl
            if thr:
                d = np.where(np.abs(d) < thr, 0, d)
            out = np.clip(np.floor(out + a * d + 0.5), 0, 255)
    return out.astype(np.int64)


def sep_apply(Wy, Wx, img):
    """rows then columns: Wy (H' x H), Wx (W' x W), img H x W x C -> H' x W' x C."""
    H, W, C = img.shape
    t = (Wy @ img.reshape(H, W * C)).reshape(-1, W, C)
    return (t.transpose(0, 2, 1) @ Wx.T).transpose(0, 2, 1)


def _blur_matrix(n, g):
    B = np.zeros((n, n))
    rad = len(g) // 2
    for i in range(n):
        for k, gv in enumerate(g):
            B[i, min(n - 1, max(0, i + k - rad))] += gv
    return B


# ---- candidates ------------------------------------------------------------------------------------

def resolve(cand, ch):
    p = copy.deepcopy(DEFAULTS)
    for k, v in cand.items():
        if k not in p and k not in ('note', 'label'):
            raise KeyError('unknown candidate parameter %r in %s' % (k, cand.get('name')))
        p[k] = v
    for max_h, ov in (cand.get('by_height') or []):
        if ch <= max_h:
            for k, v in ov.items():
                p[k] = v
            break
    if p['master_w'] is None:
        p['master_w'] = int(round(p['master_h'] * 240 / 336))
    return p


def card_size(ch):
    return int(math.floor(71 * ch / 96 + 0.5)), ch


def render_card(code, ch, cand):
    """Final premultiplied RGBA sprite (int HxWx4) for one card at height ch."""
    cw, ch = card_size(ch)
    p = resolve(cand, ch)
    svg = prep_svg(code, p)
    if p['source'] == 'vector':
        rgb = clean_master(rasterise(svg, cw, ch))
    else:
        m = clean_master(rasterise(svg, p['master_w'], p['master_h']))
        rgb = resample(m, cw, ch, p)
    return card_finish(rgb)


def xp_card(code, ch):
    """XP cards.dll bitmap, corner pixels transparent, nearest-scaled to cw x ch (premult RGBA)."""
    r, s = RANKS.index(code[0]), SUITS.index(code[1])
    im = Image.open(XP_DIR / ('cards_bitmap_%d.png' % (s * 13 + r + 1))).convert('RGBA')
    a = np.array(im)
    for (y, x) in ((0, 0), (0, 1), (1, 0)):
        for yy, xx in ((y, x), (y, 70 - x), (95 - y, x), (95 - y, 70 - x)):
            a[yy, xx] = 0
    cw, ch = card_size(ch)
    a = np.array(Image.fromarray(a).resize((cw, ch), Image.NEAREST)).astype(np.int64)
    a[..., :3] = mul255(a[..., :3], a[..., 3:4])
    return a


def column_image(sprites, ch):
    cw = sprites[0].shape[1]
    step = 9 * ch // 46
    H = ch + step * (len(sprites) - 1)
    canvas = np.zeros((H, cw, 4), np.int64)
    canvas[..., :3] = TABLE
    canvas[..., 3] = 255
    for i, s in enumerate(sprites):
        y = i * step
        canvas[y:y + ch] = over(s, canvas[y:y + ch])
    return canvas


# ---- metrics -----------------------------------------------------------------------------------------

def luma(rgb):
    return 0.299 * rgb[..., 0] + 0.587 * rgb[..., 1] + 0.114 * rgb[..., 2]


def _minmax_filter(a, k, fn):
    pad = k // 2
    p = np.pad(a, [(pad, pad), (pad, pad)] + [(0, 0)] * (a.ndim - 2), mode='edge')
    out = None
    H, W = a.shape[:2]
    for dy in range(k):
        for dx in range(k):
            v = p[dy:dy + H, dx:dx + W]
            out = v if out is None else fn(out, v)
    return out


def card_metrics(code, img, ref, ch):
    """img/ref: composited RGB float (on the table). Returns dict of metrics."""
    cw = img.shape[1]
    m = {}
    mg = max(2, int(math.ceil(frame_px(ch))) + 1)
    inner = (slice(mg, ch - mg), slice(mg, cw - mg))
    # acutance on the top-left index (rank + suit glyph)
    ix = (slice(mg, int(math.ceil(ch * 0.33))), slice(mg, int(math.ceil(cw * 0.215))))
    Y = luma(img)
    gy, gx = np.gradient(Y)
    gm = np.hypot(gx, gy)
    m['acut'] = float(gm[ix].mean())
    if ref is not None:
        Yr = luma(ref)
        gyr, gxr = np.gradient(Yr)
        m['acut_ref'] = float(np.hypot(gxr, gyr)[ix].mean())
        # overshoot beyond the 3x3 local range of the reference, near edges
        mx = _minmax_filter(ref, 3, np.maximum)
        mn = _minmax_filter(ref, 3, np.minimum)
        ov = np.maximum(img - mx, 0) + np.maximum(mn - img, 0)
        rng = _minmax_filter(Yr, 3, np.maximum) - _minmax_filter(Yr, 3, np.minimum)
        near = _minmax_filter((rng > 40).astype(float), 5, np.maximum) > 0
        sel = near[inner]
        ovi = ov[inner][sel]
        m['halo'] = float(ovi.mean()) if ovi.size else 0.0
        m['halo_vis'] = float((ovi > 12).mean() * 100) if ovi.size else 0.0
        m['mae'] = float(np.abs(img - ref)[inner].mean())
    if code in COURTS or code[0] in 'JQK':
        cx = (slice(int(ch * 0.17), int(ch * 0.83)), slice(int(cw * 0.21), int(cw * 0.79)))
        reg = img[cx]
        m['chroma'] = float((reg.max(axis=2) - reg.min(axis=2)).mean())
        m['contrast'] = float(luma(reg).std())
        m['ink'] = float((reg.max(axis=2) < 110).mean() * 100)
        m['lum'] = float(luma(reg).mean())
    return m


# ---- sheets --------------------------------------------------------------------------------------------

def _font(px):
    for f in ('/System/Library/Fonts/Supplemental/Arial.ttf', '/System/Library/Fonts/Helvetica.ttc',
              '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'):
        if os.path.exists(f):
            return ImageFont.truetype(f, px)
    return ImageFont.load_default()


def to_rgb8(a):
    return Image.fromarray(np.clip(a[..., :3], 0, 255).astype(np.uint8), 'RGB')


def build_rows(cands, ch, cards, column, xp=True, jobs=8):
    """[(label, [RGB images...])] with the XP row first (if xp)."""
    rows = []
    if xp and have_xp():
        imgs = [to_rgb8(on_table(xp_card(c, ch))) for c in cards]
        if column:
            imgs.append(to_rgb8(column_image([xp_card(c, ch) for c in COLUMN], ch)))
        rows.append(('XP original', imgs))

    def one(cand):
        sp = {c: render_card(c, ch, cand) for c in set(cards) | (set(COLUMN) if column else set())}
        imgs = [to_rgb8(on_table(sp[c])) for c in cards]
        if column:
            imgs.append(to_rgb8(column_image([sp[c] for c in COLUMN], ch)))
        return (cand.get('label', cand['name']), imgs)

    with ThreadPoolExecutor(jobs) as ex:
        rows += list(ex.map(one, cands))
    return rows


def compose(rows, ch, zoom=1, title=None, label_w=None, font_px=14):
    gap = max(4, ch // 16) * zoom
    font = _font(font_px)
    if label_w is None:
        label_w = max(int(font.getlength(lbl)) for lbl, _ in rows) + 16
    rh = max(im.height for _, ims in rows for im in ims) * zoom + gap
    W = label_w + sum(im.width * zoom + gap for im in rows[0][1]) + gap
    top = 24 if title else gap
    sheet = Image.new('RGB', (W, top + rh * len(rows)), TABLE)
    d = ImageDraw.Draw(sheet)
    if title:
        d.text((8, 4), title, fill=(255, 255, 255), font=font)
    for r, (lbl, ims) in enumerate(rows):
        y = top + r * rh
        d.text((8, y + 4), lbl, fill=(255, 255, 255), font=font)
        x = label_w
        for im in ims:
            if zoom != 1:
                im = im.resize((im.width * zoom, im.height * zoom), Image.NEAREST)
            sheet.paste(im, (x, y))
            x += im.width + gap
    return sheet


def compose_t(rows, zoom, title, font_px=12):
    """Transposed layout: one COLUMN per candidate (name wrapped above it), cards stacked."""
    font = _font(font_px)
    gap = 4 * zoom
    colw = [max(im.width for im in ims) * zoom for _, ims in rows]
    heights = [max(rows[c][1][r].height for c in range(len(rows))) * zoom for r in range(len(rows[0][1]))]

    def wrap(t, w):
        lines, cur = [], ''
        for word in t.replace('+', '+ ').split(' '):
            nxt = (cur + ' ' + word).strip() if cur and not cur.endswith('+') else cur + word
            if cur and font.getlength(nxt) > w:
                lines.append(cur)
                cur = word
            else:
                cur = nxt
        return lines + [cur]
    labels = [wrap(lbl, cw) for (lbl, _), cw in zip(rows, colw)]
    head = 22 + (font_px + 2) * max(len(l) for l in labels)
    W = gap + sum(c + gap for c in colw)
    H = head + sum(h + gap for h in heights)
    sheet = Image.new('RGB', (max(W, int(font.getlength(title)) + 16), H), TABLE)
    d = ImageDraw.Draw(sheet)
    d.text((gap, 3), title, fill=(255, 255, 255), font=font)
    x = gap
    for (lbl, ims), cw, ls in zip(rows, colw, labels):
        for i, line in enumerate(ls):
            d.text((x, 20 + i * (font_px + 2)), line, fill=(255, 255, 255), font=font)
        y = head
        for im, h in zip(ims, heights):
            sheet.paste(im.resize((im.width * zoom, im.height * zoom), Image.NEAREST), (x, y))
            y += h + gap
        x += cw + gap
    return sheet


def write_sheets(cands, heights, out, cards=CARDS, column=True, zoom_max=160, tag='', jobs=8):
    out.mkdir(parents=True, exist_ok=True)
    paths = []
    for ch in heights:
        rows = build_rows(cands, ch, cards, column, jobs=jobs)
        cw, _ = card_size(ch)
        title = 'h=%d (%dx%d card, %.2fx downscale from 400x560)' % (ch, cw, ch, 560 / ch)
        s = compose(rows, ch, 1, title)
        p = out / ('%sh%d.png' % (tag, ch))
        s.save(p, optimize=True)
        paths.append(p)
        if ch <= zoom_max:
            s2 = compose(rows, ch, 2, title)
            p2 = out / ('%sh%d_x2.png' % (tag, ch))
            s2.save(p2, optimize=True)
            paths.append(p2)
    return paths


# region crops (fractions of the card: y0, y1, x0, x1); 'strips' = the column's buried index strips
REGIONS = {
    'index': (0.0, 0.36, 0.0, 0.30),
    'court': (0.15, 0.55, 0.18, 0.82),
    'centre': (0.25, 0.75, 0.15, 0.85),
    'corner': (0.0, 0.2, 0.0, 0.2),
    'full': (0.0, 1.0, 0.0, 1.0),
}


def write_crops(cands, heights, out, cards, region, zoom, jobs=8, transpose=False):
    """Zoomed crops: rows = XP + candidates, columns = cards (+ 'col' for the column's strips)."""
    out.mkdir(parents=True, exist_ok=True)
    paths = []
    for ch in heights:
        cw, _ = card_size(ch)
        step = 9 * ch // 46
        rows = build_rows(cands, ch, [c for c in cards if c != 'col'], 'col' in cards, jobs=jobs)
        crow = []
        for lbl, ims in rows:
            cut = []
            for im in ims:
                y0, y1, x0, x1 = REGIONS[region]
                if im.height > ch:          # the column: its top strips plus the top of the last card
                    cut.append(im.crop((int(x0 * cw), 0, int(math.ceil(x1 * cw)), 4 * step + int(0.36 * ch))))
                else:
                    cut.append(im.crop((int(x0 * cw), int(y0 * ch), int(math.ceil(x1 * cw)),
                                        int(math.ceil(y1 * ch)))))
            crow.append((lbl, cut))
        hmax = max(c.height for _, cs in crow for c in cs)
        if transpose:
            s = compose_t(crow, zoom, 'h=%d %s x%d' % (ch, region, zoom))
        else:
            s = compose(crow, max(8, hmax // 2), zoom, 'h=%d %s x%d' % (ch, region, zoom))
        p = out / ('%s_h%d_x%d.png' % (region, ch, zoom))
        s.save(p, optimize=True)
        paths.append(p)
    return paths


# ---- metrics over a round -------------------------------------------------------------------------------

METRIC_KEYS = ['acut', 'acut_ref', 'halo', 'halo_vis', 'mae', 'chroma', 'contrast', 'ink', 'lum']


def compute_metrics(cands, heights, cards=CARDS, jobs=8):
    rows = []

    def one(args):
        cand, ch = args
        vec = dict(cand)
        vec['source'] = 'vector'
        vec.pop('by_height', None)
        res = []
        p = resolve(cand, ch)
        vec.update({k: p[k] for k in ('index_stroke', 'court_mult', 'court_colour', 'court_fill',
                                      'court_frame_w', 'court_frame_colour', 'colour_map')})
        for c in cards:
            img = on_table(render_card(c, ch, cand))[..., :3].astype(float)
            ref = on_table(render_card(c, ch, vec))[..., :3].astype(float)
            res.append(card_metrics(c, img, ref, ch))
        agg = {'name': cand['name'], 'h': ch}
        for k in METRIC_KEYS:
            v = [r[k] for r in res if k in r]
            agg[k] = round(float(np.mean(v)), 3) if v else ''
        return agg

    with ThreadPoolExecutor(jobs) as ex:
        rows = list(ex.map(one, [(c, h) for c in cands for h in heights]))
    if have_xp():
        for ch in heights:
            res = [card_metrics(c, on_table(xp_card(c, ch))[..., :3].astype(float), None, ch) for c in cards]
            agg = {'name': 'XP original', 'h': ch}
            for k in METRIC_KEYS:
                v = [r[k] for r in res if k in r]
                agg[k] = round(float(np.mean(v)), 3) if v else ''
            rows.append(agg)
    return rows


def write_metrics(rows, out, heights):
    out.mkdir(parents=True, exist_ok=True)
    with open(out / 'metrics.csv', 'w', newline='') as f:
        w = csv.DictWriter(f, ['name', 'h'] + METRIC_KEYS)
        w.writeheader()
        w.writerows(rows)
    names = list(dict.fromkeys(r['name'] for r in rows))
    by = {(r['name'], r['h']): r for r in rows}
    md = ['# metrics', '',
          'acut = mean |grad luma| on the top-left index (higher = crisper; acut_ref = same art rendered '
          'as vector at target size); halo = mean overshoot (0-255) beyond the 3x3 local range of the '
          'vector reference near edges, halo_vis = %% of those samples overshooting > 12; mae = mean '
          'abs error vs the vector reference; chroma/contrast/ink/lum = court picture (J/Q/K only): '
          'mean RGB max-min, luma std, ink = %% pixels with max(R,G,B) < 110 (dark linework), mean luma.', '']
    for k in ['acut', 'halo', 'halo_vis', 'mae', 'chroma', 'contrast', 'ink']:
        md.append('## %s' % k)
        md.append('')
        md.append('| candidate | ' + ' | '.join('h%d' % h for h in heights) + ' |')
        md.append('|---|' + '---|' * len(heights))
        for n in names:
            vals = [by.get((n, h), {}).get(k, '') for h in heights]
            md.append('| %s | ' % n + ' | '.join(('%.2f' % v) if v != '' else '' for v in vals) + ' |')
        md.append('')
    (out / 'metrics.md').write_text('\n'.join(md))


# ---- artefact checks ---------------------------------------------------------------------------------

def artefacts(cands, heights, jobs=8):
    """Per candidate/height: shape (alpha) identical to 'current'; outside-card pixels untouched; red
    pip hue (mean |G-B| on 8H pixels with R >= 200) and dark fringe count (8H pixels with R < 200
    and luma < 60, i.e. darker than pure red); court 'noise' = mean |laplacian| of luma in the court
    picture relative to the vector reference of the same art (1.0 = as busy as ideal AA)."""
    base = {'name': 'current'}
    rows = []

    def lap(Y):
        p = np.pad(Y, 1, mode='edge')
        return np.abs(4 * Y - p[:-2, 1:-1] - p[2:, 1:-1] - p[1:-1, :-2] - p[1:-1, 2:])

    def one(args):
        cand, ch = args
        cw, _ = card_size(ch)
        r = {'name': cand['name'], 'h': ch}
        s8 = render_card('8H', ch, cand)
        b8 = render_card('8H', ch, base)
        r['alpha_diff'] = int(np.abs(s8[..., 3] - b8[..., 3]).max())
        t8 = on_table(s8)[..., :3]
        outside = s8[..., 3] == 0
        r['outside_diff'] = int(np.abs(t8[outside] - np.array(TABLE)).max()) if outside.any() else 0
        reds = t8[(t8[..., 0] >= 200)]
        r['red_GB'] = round(float(np.abs(reds[:, 1] - reds[:, 2]).mean()), 3)
        Y = luma(t8.astype(float))
        mg = max(2, int(math.ceil(frame_px(ch))) + 1)
        inner = (slice(mg, ch - mg), slice(mg, cw - mg))
        r['red_fringe'] = int(((t8[..., 0] < 200) & (Y < 60))[inner].sum())
        p = resolve(cand, ch)
        vec = {k: p[k] for k in ('index_stroke', 'court_mult', 'court_colour', 'court_fill',
                                 'court_frame_w', 'court_frame_colour', 'colour_map')}
        vec.update(name='vec', source='vector')
        nz = []
        for c in COURTS:
            a = luma(on_table(render_card(c, ch, cand))[..., :3].astype(float))
            b = luma(on_table(render_card(c, ch, vec))[..., :3].astype(float))
            sl = (slice(int(ch * 0.17), int(ch * 0.83)), slice(int(cw * 0.21), int(cw * 0.79)))
            nz.append(lap(a)[sl].mean() / max(1e-6, lap(b)[sl].mean()))
        r['court_noise'] = round(float(np.mean(nz)), 3)
        return r

    with ThreadPoolExecutor(jobs) as ex:
        rows = list(ex.map(one, [(c, h) for c in cands for h in heights]))
    return rows


# ---- CLI ---------------------------------------------------------------------------------------------------

def load_cands(path, only=None):
    data = json.loads(Path(path).read_text())
    cands = data['candidates'] if isinstance(data, dict) else data
    if only:
        keep = only.split(',')
        cands = [c for c in cands if c['name'] in keep]
        cands.sort(key=lambda c: keep.index(c['name']))
    return cands


def main(argv=None):
    global XP_DIR, CACHE
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('cmd', choices=['sheets', 'metrics', 'all', 'blind', 'card', 'crops', 'artefacts'])
    ap.add_argument('--candidates', '-c', required=True, help='JSON file: list of candidate dicts')
    ap.add_argument('--only', help='comma-separated candidate names (in this order)')
    ap.add_argument('--heights', default=','.join(map(str, HEIGHTS)))
    ap.add_argument('--cards', default=','.join(CARDS))
    ap.add_argument('--no-column', action='store_true')
    ap.add_argument('--out', '-o', required=True)
    ap.add_argument('--xp', default=str(XP_DIR) if str(XP_DIR) else None, help='dir of XP cards_bitmap_<id>.png')
    ap.add_argument('--cache', default=str(CACHE))
    ap.add_argument('--seed', type=int, default=None, help='blind: shuffle seed')
    ap.add_argument('--zoom-max', type=int, default=160)
    ap.add_argument('--jobs', type=int, default=8)
    ap.add_argument('--region', default='index', choices=sorted(REGIONS))
    ap.add_argument('--zoom', type=int, default=4)
    ap.add_argument('--transpose', '-t', action='store_true', help='crops: one column per candidate')
    args = ap.parse_args(argv)
    if args.xp:
        XP_DIR = Path(args.xp)
    CACHE = Path(args.cache)
    heights = [int(h) for h in args.heights.split(',')]
    cards = args.cards.split(',')
    cands = load_cands(args.candidates, args.only)
    out = Path(args.out)
    if args.cmd in ('sheets', 'all'):
        for p in write_sheets(cands, heights, out, cards, not args.no_column, args.zoom_max, jobs=args.jobs):
            print(p)
    if args.cmd in ('metrics', 'all'):
        rows = compute_metrics(cands, heights, jobs=args.jobs)
        write_metrics(rows, out, heights)
        print(out / 'metrics.csv')
    if args.cmd == 'blind':
        rnd = random.Random(args.seed)
        order = list(range(len(cands)))
        rnd.shuffle(order)
        letters = 'ABCDEFGH'
        blind = []
        mapping = {}
        for i, ci in enumerate(order):
            c = dict(cands[ci])
            c['label'] = letters[i]
            mapping[letters[i]] = {'name': cands[ci]['name'], 'params': cands[ci]}
            blind.append(c)
        out.mkdir(parents=True, exist_ok=True)
        (out / 'mapping.json').write_text(json.dumps(mapping, indent=2))
        for ch in heights:
            rows = build_rows(blind, ch, cards, not args.no_column, jobs=args.jobs)
            rows[0] = ('XP', rows[0][1])
            title = 'h=%d' % ch
            compose(rows, ch, 1, title, label_w=40, font_px=16).save(out / ('h%d.png' % ch), optimize=True)
            if ch <= args.zoom_max:
                compose(rows, ch, 2, title, label_w=40, font_px=16).save(out / ('h%d_x2.png' % ch), optimize=True)
            print(out / ('h%d.png' % ch))
    if args.cmd == 'artefacts':
        rows = artefacts(cands, heights, args.jobs)
        out.mkdir(parents=True, exist_ok=True)
        with open(out / 'artefacts.csv', 'w', newline='') as f:
            w = csv.DictWriter(f, list(rows[0].keys()))
            w.writeheader()
            w.writerows(rows)
        for r in rows:
            print(' '.join('%s=%s' % kv for kv in r.items()))
    if args.cmd == 'crops':
        for p in write_crops(cands, heights, out, cards, args.region, args.zoom, args.jobs,
                             args.transpose):
            print(p)
    if args.cmd == 'card':
        for ch in heights:
            for cand in cands:
                for c in cards:
                    im = to_rgb8(on_table(render_card(c, ch, cand)))
                    p = out / ('%s_%s_h%d.png' % (cand['name'], c, ch))
                    out.mkdir(parents=True, exist_ok=True)
                    im.save(p)
                    print(p)


if __name__ == '__main__':
    main()
