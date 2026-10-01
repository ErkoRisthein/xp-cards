#!/usr/bin/env python3
"""Cross-check crisplab's numpy model against C.

  check.py runtime <runtime_card binary>     current pipeline vs the unchanged runtime C code
  check.py shipped <runtime_card binary>     the shipped masters through the runtime's
                                              fc_image_resample_card vs candidate
                                              'art+darkbias+sharpen+clamp' (candidates/shipped.json)
  check.py proto <resample_card binary> <masters dir>
                                              finalists art+darkbias / art+darkbias+sharpen vs the
                                              integer C prototype (masters = PNGs of the new art)

Build (from the repo root):
  cc -O2 -w -Isrc/gfx -Isrc/core -Ithird_party -o /tmp/runtime_card tools/crisplab/proto/runtime_card.c -lm
  cc -O2 -w -Isrc/gfx -Isrc/core -Ithird_party -o /tmp/resample_card tools/crisplab/proto/resample_card.c -lm
Prints the max abs difference per card and height (expected: <= 1).
"""
import json
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import crisplab as cl  # noqa: E402
from PIL import Image  # noqa: E402

CARDS = ['AS', '8H', 'JD', 'KH', 'TC', 'QH', 'KS']
HEIGHTS = [72, 96, 128, 160, 200, 257, 320]


def read_raw(path, cw, ch):
    raw = np.fromfile(path, dtype='<u4').reshape(ch, cw)
    return np.stack([(raw >> 16) & 255, (raw >> 8) & 255, raw & 255, raw >> 24], -1).astype(np.int64)


def main():
    mode, binary = sys.argv[1], sys.argv[2]
    worst = 0
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / 'o.raw'
        if mode in ('runtime', 'shipped'):
            if mode == 'runtime':
                cur, extra = {'name': 'current'}, []
            else:
                cands = json.loads((HERE.parent / 'candidates' / 'shipped.json').read_text())
                cur, extra = [c for c in cands if c['name'] == 'art+darkbias+sharpen+clamp'][0], ['card']
            for c in CARDS:
                png = cl.REPO / 'res' / 'cards' / (c + '.png')
                ship = np.asarray(Image.open(png).convert('RGBA'))
                for h in HEIGHTS:
                    cw, ch = cl.card_size(h)
                    subprocess.run([binary, str(png), str(h), str(out)] + extra, check=True, capture_output=True)
                    ref = read_raw(out, cw, ch)
                    p = cl.resolve(cur, h)
                    mine = cl.card_finish(cl.resample(cl.clean_master(ship), cw, ch, p))
                    d = int(np.abs(mine - ref).max())
                    worst = max(worst, d)
                    print(c, h, 'max diff', d)
        else:
            masters = Path(sys.argv[3])
            fin = {c['name']: c for c in json.loads((HERE.parent / 'candidates' / 'finalists.json').read_text())}
            for name, g, a in (('art+darkbias', 0.5, 0.0), ('art+darkbias+sharpen', 0.6, 0.2)):
                for c in CARDS:
                    for h in HEIGHTS[:-1]:
                        cw, ch = cl.card_size(h)
                        subprocess.run([binary, str(masters / (c + '.png')), str(h), str(g), str(a), str(out)],
                                       check=True, capture_output=True)
                        d = int(np.abs(cl.render_card(c, h, fin[name]) - read_raw(out, cw, ch)).max())
                        worst = max(worst, d)
                        print(name, c, h, 'max diff', d)
    print('worst', worst)
    return 0 if worst <= 1 else 1


if __name__ == '__main__':
    sys.exit(main())
