# Standalone version of the proposed art edit for tools/make_assets.sh (pixel-identical to
# crisplab.prep_svg): usage edit_svg.py <in.svg> <out.svg> <index_stroke> <court_mult> <colour> <frame_w>
import re, sys
src, dst, idx, mult, col, fw = sys.argv[1:7]
s = open(src).read().replace('stroke-width="80"', 'stroke-width="%s"' % idx)
def edit(m):
    t = m.group(0)
    if 'stroke="#44F"' not in t:
        return t
    if 'xlink:href="#X' in t:   # frame around the court picture (default width 1)
        return t.replace('stroke="#44F"', 'stroke="%s" stroke-width="%s"' % (col, fw))
    t = re.sub(r'stroke-width="([0-9.]+)"', lambda w: 'stroke-width="%.4g"' % (float(w.group(1)) * float(mult)), t)
    return t.replace('stroke="#44F"', 'stroke="%s"' % col)
open(dst, 'w').write(re.sub(r'<(?:path|use)\b[^>]*>', edit, s))
