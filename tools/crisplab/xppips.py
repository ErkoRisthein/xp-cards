#!/usr/bin/env python3
"""xppips: measure the Windows XP cards.dll pip geometry -> candidates/xp_pips.json (stacklab layout 'xp_pips').

usage: xppips.py --xp DIR [-o candidates/xp_pips.json] [--full full.json] [--overlay overlay.png]
  DIR holds the XP bitmaps cards_bitmap_<id>.png (71x96, id = suit*13 + rank + 1, suits C D H S, ranks A..K),
  as for crisplab's CRISPLAB_XP.

Pips = 8-connected components of the suit colour (black / pure red), minus the 1-px border and the two index
corners (x < 15, y < 30 and the rotated copy). Boxes are ink bounding boxes in continuous pixel coordinates
(pixel i covers [i, i+1)), normalised by 71 (x) and 96 (y). Orientation = exact match of the pip bitmap with
the suit's upright template (the 2's top pip) or its 180-degree turn. Court frame = the rows/columns of black
longer than 55-60% of the card. The frozen output is symmetrised: the pip columns snapped to one left / centre /
right x for all ranks (mean of every measured left / right pip), every arrangement made symmetric under the
180-degree turn (pairs averaged, a self-paired pip put on the centre), suits that deviate by > 6 px from the
spades' arrangement kept as 'quirks' (XP's 8D is 2-1-2-1-2). Needs numpy and Pillow only.
"""
import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

XP = Path('.')
RANKS = 'A23456789TJQK'
SUITS = 'CDHS'
W, H = 71, 96


def load(code):
    r, s = RANKS.index(code[0]), SUITS.index(code[1])
    return np.array(Image.open(XP / ('cards_bitmap_%d.png' % (s * 13 + r + 1))).convert('RGB'))


def suit_mask(a, suit):
    if suit in 'CS':
        return (a == 0).all(2)
    return (a[..., 0] == 255) & (a[..., 1] == 0) & (a[..., 2] == 0)


def interior(m):
    m = m.copy()
    m[0, :] = m[-1, :] = False
    m[:, 0] = m[:, -1] = False
    for (y, x) in ((1, 1), (1, W - 2), (H - 2, 1), (H - 2, W - 2)):
        m[y, x] = False
    return m


IDX_TL = (0, 0, 15, 30)          # x0, y0, x1, y1 (exclusive) — rank + suit index, top-left
IDX_BR = (W - 15, H - 30, W, H)  # rotated index


def inside(b, box):
    return b[0] >= box[0] and b[1] >= box[1] and b[2] <= box[2] and b[3] <= box[3]


def label8(m):
    """8-connected component labels (0 = background) of a small boolean image."""
    lab = np.zeros(m.shape, int)
    n = 0
    for y0, x0 in zip(*np.nonzero(m)):
        if lab[y0, x0]:
            continue
        n += 1
        lab[y0, x0] = n
        stack = [(y0, x0)]
        while stack:
            y, x = stack.pop()
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    yy, xx = y + dy, x + dx
                    if 0 <= yy < m.shape[0] and 0 <= xx < m.shape[1] and m[yy, xx] and not lab[yy, xx]:
                        lab[yy, xx] = n
                        stack.append((yy, xx))
    return lab, n


def comps(m):
    lab, n = label8(m)
    out = []
    for i in range(1, n + 1):
        ys, xs = np.nonzero(lab == i)
        b = (int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1)
        out.append((b, lab[b[1]:b[3], b[0]:b[2]] == i))
    return out


def orientation(patch, tmpl):
    """'up' / 'rot' / 'sym' by matching the pip bitmap against the suit's upright template."""
    if np.array_equal(patch, patch[::-1, ::-1]):
        return 'sym'
    if tmpl is not None and patch.shape == tmpl.shape:
        if np.array_equal(patch, tmpl):
            return 'up'
        if np.array_equal(patch, tmpl[::-1, ::-1]):
            return 'rot'
    # fallback: mass centre vs bbox centre, compared with the template's sign
    ys = np.nonzero(patch)[0] + 0.5
    d = ys.mean() - patch.shape[0] / 2
    ts = np.nonzero(tmpl)[0] + 0.5
    dt = ts.mean() - tmpl.shape[0] / 2
    return 'up' if np.sign(d) == np.sign(dt) else 'rot'


def measure():
    tmpl = {}
    for s in SUITS:      # upright template: the 2's top pip
        cs = [c for c in comps(interior(suit_mask(load('2' + s), s))) if not inside(c[0], IDX_TL) and not inside(c[0], IDX_BR)]
        cs.sort(key=lambda c: c[0][1])
        tmpl[s] = cs[0][1]
    cards = {}
    for s in SUITS:
        for r in RANKS:
            code = r + s
            a = load(code)
            m = interior(suit_mask(a, s))
            rec = {'code': code}
            if r in 'JQK':
                # frame = the long dark lines (black rows/cols spanning > 60% of the picture)
                k = (a == 0).all(2)
                rows = [y for y in range(2, H - 2) if k[y, 2:W - 2].sum() > 0.55 * W]
                cols = [x for x in range(2, W - 2) if k[2:H - 2, x].sum() > 0.6 * H]
                fx0, fx1, fy0, fy1 = min(cols), max(cols) + 1, min(rows), max(rows) + 1
                rec['frame'] = [fx0, fy0, fx1, fy1]
                # court pips: suit-colour components of pip size inside the frame
                cs = []
                for b, p in comps(m):
                    if inside(b, IDX_TL) or inside(b, IDX_BR):
                        continue
                    w, h = b[2] - b[0], b[3] - b[1]
                    if 11 <= h <= 17 and 9 <= w <= 17 and p.sum() > 60:
                        cs.append({'box': list(b), 'rot': orientation(p, tmpl[s])})
                rec['court_pips'] = cs
            else:
                pips = []
                for b, p in comps(m):
                    if inside(b, IDX_TL) or inside(b, IDX_BR):
                        continue
                    if p.sum() < 20:
                        continue
                    pips.append({'box': list(b), 'rot': orientation(p, tmpl[s]) if r != 'A' or s != 'S' else 'up',
                                 'n': int(p.sum())})
                pips.sort(key=lambda q: (q['box'][1] + q['box'][3], q['box'][0]))
                rec['pips'] = pips
            cards[code] = rec
    return cards, tmpl


def norm_box(b):
    x0, y0, x1, y1 = b
    return {'cx': (x0 + x1) / 2 / W, 'cy': (y0 + y1) / 2 / H, 'w': (x1 - x0) / W, 'h': (y1 - y0) / H}


def summarise(cards):
    g = {'source': 'Windows XP cards.dll bitmaps 1-52 (71x96), measured by tools/crisplab/xppips.py',
         'units': 'fractions of the card: x / 71, y / 96; boxes are ink bounding boxes in continuous pixel '
                  'coordinates (pixel i covers [i, i+1)); rot = pip drawn rotated 180 degrees',
         'card_px': [W, H]}
    # pip sizes per suit (body pips, 2-10)
    sz = {}
    for s in SUITS:
        ws = [p['box'][2] - p['box'][0] for r in '23456789T' for p in cards[r + s]['pips']]
        hs = [p['box'][3] - p['box'][1] for r in '23456789T' for p in cards[r + s]['pips']]
        sz[s] = {'w_px': float(np.median(ws)), 'h_px': float(np.median(hs)),
                 'w': float(np.median(ws)) / W, 'h': float(np.median(hs)) / H}
    g['pip_size'] = sz
    ranks = {}
    for r in '23456789T':
        n = {len(cards[r + s]['pips']) for s in SUITS}
        assert len(n) == 1, (r, n)
        pl = []
        for i in range(n.pop()):
            bs = [cards[r + s]['pips'][i] for s in SUITS]
            cx = np.mean([(b['box'][0] + b['box'][2]) / 2 for b in bs])
            cy = np.mean([(b['box'][1] + b['box'][3]) / 2 for b in bs])
            rots = [b['rot'] for b in bs if b['rot'] != 'sym']
            rot = max(set(rots), key=rots.count) if rots else 'sym'
            pl.append({'cx': round(cx / W, 5), 'cy': round(cy / H, 5), 'cx_px': round(cx, 2), 'cy_px': round(cy, 2),
                       'rot': rot == 'rot', 'rot_votes': rots,
                       'spread_px': round(float(max(np.ptp([(b['box'][0] + b['box'][2]) / 2 for b in bs]),
                                                    np.ptp([(b['box'][1] + b['box'][3]) / 2 for b in bs]))), 2)})
        ranks[r] = pl
    g['ranks'] = ranks
    g['ace'] = {s: dict(norm_box(cards['A' + s]['pips'][0]['box']), box_px=cards['A' + s]['pips'][0]['box'])
                for s in SUITS}
    fr = {}
    for r in 'JQK':
        for s in SUITS:
            fr[r + s] = cards[r + s]['frame']
    g['court_frame_px'] = fr
    fb = np.array(list(fr.values()), float)
    med = np.median(fb, 0)
    g['court_frame'] = {'box_px': med.tolist(), 'x0': med[0] / W, 'y0': med[1] / H, 'x1': med[2] / W, 'y1': med[3] / H,
                        'note': 'outer edge of the 1-px frame line'}
    cp = {}
    for r in 'JQK':
        for s in SUITS:
            ps = cards[r + s]['court_pips']
            ps = sorted(ps, key=lambda q: q['box'][1])
            if ps:
                b = ps[0]['box']
                cp[r + s] = dict(norm_box(b), box_px=b, side='left' if (b[0] + b[2]) / 2 < W / 2 else 'right',
                                 n=len(ps))
    g['court_pip'] = cp
    g['per_card'] = {c: {k: v for k, v in rec.items() if k != 'code'} for c, rec in cards.items()}
    return g


def overlays(cards, path, zoom=5):
    tiles = []
    for s in 'SH':
        for r in RANKS:
            code = r + s
            a = load(code)
            im = Image.fromarray(a).resize((W * zoom, H * zoom), Image.NEAREST)
            d = ImageDraw.Draw(im)
            rec = cards[code]
            for p in rec.get('pips', []):
                x0, y0, x1, y1 = p['box']
                col = (0, 160, 255) if p['rot'] == 'up' else ((255, 0, 255) if p['rot'] == 'rot' else (0, 200, 0))
                d.rectangle([x0 * zoom, y0 * zoom, x1 * zoom - 1, y1 * zoom - 1], outline=col, width=2)
                cx, cy = (x0 + x1) / 2 * zoom, (y0 + y1) / 2 * zoom
                d.line([cx - 4, cy, cx + 4, cy], fill=col, width=2)
                d.line([cx, cy - 4, cx, cy + 4], fill=col, width=2)
            if 'frame' in rec:
                x0, y0, x1, y1 = rec['frame']
                d.rectangle([x0 * zoom, y0 * zoom, x1 * zoom - 1, y1 * zoom - 1], outline=(0, 200, 0), width=2)
                for p in rec['court_pips']:
                    x0, y0, x1, y1 = p['box']
                    d.rectangle([x0 * zoom, y0 * zoom, x1 * zoom - 1, y1 * zoom - 1], outline=(0, 160, 255), width=2)
            for bx in (IDX_TL, IDX_BR):
                d.rectangle([bx[0] * zoom, bx[1] * zoom, bx[2] * zoom - 1, bx[3] * zoom - 1], outline=(255, 160, 0), width=1)
            tiles.append(im)
    cols = 7
    tw, th = W * zoom + 10, H * zoom + 10
    sheet = Image.new('RGB', (cols * tw + 10, ((len(tiles) + cols - 1) // cols) * th + 10), (0, 100, 0))
    for i, t in enumerate(tiles):
        sheet.paste(t, (10 + (i % cols) * tw, 10 + (i // cols) * th))
    sheet.save(path, optimize=True)


# ---- frozen, symmetrised geometry for stacklab (tools/crisplab/candidates/xp_pips.json) ----------------

def arrangement(cards, codes):
    """mean pip centres (px) over the given cards (same arrangement), with rot votes."""
    n = len(cards[codes[0]]['pips'])
    out = []
    for i in range(n):
        bs = [cards[c]['pips'][i] for c in codes]
        cx = np.mean([(b['box'][0] + b['box'][2]) / 2 for b in bs])
        cy = np.mean([(b['box'][1] + b['box'][3]) / 2 for b in bs])
        rots = [b['rot'] for b in bs if b['rot'] != 'sym']
        out.append([cx, cy, (max(set(rots), key=rots.count) == 'rot') if rots else (cy > H / 2)])
    return out


def symmetrise(pl, left, right):
    """snap x to the three columns, make the set symmetric under the 180-degree rotation about the centre."""
    pts = []
    for cx, cy, rot in pl:
        f = cx / W
        col = left if f < 0.4 else (right if f > 0.6 else 0.5)
        pts.append([col, cy / H, rot])
    out = []
    for i, (x, y, rot) in enumerate(pts):
        # partner: nearest pip to the rotated position
        j = min(range(len(pts)), key=lambda k: (pts[k][0] - (1 - x)) ** 2 + (pts[k][1] - (1 - y)) ** 2)
        if (pts[j][0] - (1 - x)) ** 2 + (pts[j][1] - (1 - y)) ** 2 > (3 / H) ** 2:
            ys = y                                 # unpaired (the 7's extra pip)
        else:
            ys = 0.5 if j == i else (y + 1 - pts[j][1]) / 2
        out.append([round(x, 5), round(ys, 5), bool(rot and ys > 0.5)])
    return out


def freeze(cards, g, path):
    lefts = [((p['box'][0] + p['box'][2]) / 2) / W for c in cards.values() if c['code'][0] in '23456789T'
             for p in c['pips'] if (p['box'][0] + p['box'][2]) / 2 / W < 0.4]
    rights = [((p['box'][0] + p['box'][2]) / 2) / W for c in cards.values() if c['code'][0] in '23456789T'
              for p in c['pips'] if (p['box'][0] + p['box'][2]) / 2 / W > 0.6]
    d = (np.mean(rights) - np.mean(lefts)) / 2
    left, right = 0.5 - d, 0.5 + d
    ranks, quirks = {}, {}
    for r in '23456789T':
        # a suit is a quirk when its arrangement differs from the spades' by more than pixel noise (6 px)
        def same(s1, s2):
            p1, p2 = cards[r + s1]['pips'], cards[r + s2]['pips']
            return len(p1) == len(p2) and all(
                abs((a['box'][0] + a['box'][2]) / 2 - (b['box'][0] + b['box'][2]) / 2) <= 6 and
                abs((a['box'][1] + a['box'][3]) / 2 - (b['box'][1] + b['box'][3]) / 2) <= 6 for a, b in zip(p1, p2))
        maj = [s for s in SUITS if same(s, 'S')]
        ranks[r] = symmetrise(arrangement(cards, [r + s for s in maj]), left, right)
        for s in SUITS:
            if s not in maj:
                quirks[r + s] = symmetrise(arrangement(cards, [r + s]), left, right)
    fr = g['court_frame']['box_px']
    k = [g['court_pip'][c]['box_px'] for c in ('KC', 'KH', 'KS')]
    kp = np.mean(k, 0)
    asb = g['ace']['S']['box_px']
    frozen = {
        'doc': 'Windows XP cards.dll pip geometry (71x96 bitmaps), measured by tools/crisplab/xppips.py and symmetrised '
               '(columns snapped to one left/centre/right x for all ranks, every pip set made symmetric under the '
               '180-degree turn). Fractions of the card: x of the width, y of the height. ranks: [cx, cy, rot] per pip '
               '(rot = drawn turned 180 degrees). quirks: cards whose XP arrangement differs from their rank '
               '(8D is 2-1-2-1-2 in XP). pip_h: XP pip ink height (15 px of 96, every suit). court_frame: the frame '
               "line's centre (top-left corner; XP: the 1-px line in column 12 / row 11). court_pip: XP's K pip "
               '(top-left, inside the frame; XP J/Q have theirs top-right). ace: A of C/D/H pip = a body pip at the '
               'centre; ace_spade: the ornate AS ink box.',
        'source_px': [W, H],
        'pip_h': g['pip_size']['S']['h'],
        'pip_w': {s: g['pip_size'][s]['w'] for s in SUITS},
        'columns': [round(left, 5), 0.5, round(right, 5)],
        'ranks': ranks,
        'quirks': quirks,
        'court_frame': [round((fr[0] + 0.5) / W, 5), round((fr[1] + 0.5) / H, 5)],
        'court_frame_outer': [fr[0] / W, fr[1] / H, fr[2] / W, fr[3] / H],
        'court_pip': {'cx': round((kp[0] + kp[2]) / 2 / W, 5), 'top': round(kp[1] / H, 5),
                      'h': round((kp[3] - kp[1]) / H, 5), 'side_by_rank': {'J': 'right', 'Q': 'right', 'K': 'left'}},
        'ace_h': round((g['ace']['H']['box_px'][3] - g['ace']['H']['box_px'][1]) / H, 5),
        'ace_spade': {'x0': asb[0] / W, 'y0': asb[1] / H, 'x1': asb[2] / W, 'y1': asb[3] / H,
                      'cx': (asb[0] + asb[2]) / 2 / W, 'cy': (asb[1] + asb[3]) / 2 / H,
                      'w': (asb[2] - asb[0]) / W, 'h': (asb[3] - asb[1]) / H},
    }
    Path(path).write_text(json.dumps(frozen, indent=1) + '\n')
    return frozen


def main(argv=None):
    global XP
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--xp', required=True, help='dir of cards_bitmap_<id>.png')
    ap.add_argument('-o', '--out', default=str(Path(__file__).resolve().parent / 'candidates' / 'xp_pips.json'))
    ap.add_argument('--full', help='also write the raw per-card measurements here')
    ap.add_argument('--overlay', help='also write an overlay sheet (S and H suits, measured boxes) here')
    a = ap.parse_args(argv)
    XP = Path(a.xp)
    cards, _ = measure()
    g = summarise(cards)
    if a.full:
        Path(a.full).write_text(json.dumps(g, indent=1))
    if a.overlay:
        overlays(cards, a.overlay)
    fz = freeze(cards, g, a.out)
    for r, pl in fz['ranks'].items():
        print(r, ' '.join('(%.4f,%.4f%s)' % (x, y, 'R' if rot else '') for x, y, rot in pl))
    print('quirks', sorted(fz['quirks']), 'columns', fz['columns'], 'frame', fz['court_frame'])
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
