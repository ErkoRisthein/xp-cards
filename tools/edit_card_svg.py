#!/usr/bin/env python3
"""FreeCell HD: the art edit applied to a RevK card SVG before rasterising (tools/make_assets.sh cards).

usage: edit_card_svg.py <in.svg> <out.svg> <index_stroke> <court_mult> <court_colour> <court_frame_w>

  index_stroke   every stroke-width="80" (the generator's rank-index strokes) becomes this width
  court_mult     every <path>/<use> with stroke="#44F" (the court-art linework in J/Q/K: widths 3, 6,
                 36, 42.352, 48, 55.384, 65.454, 72 in the 1300x2000 court art) gets its stroke-width
                 multiplied by this ...
  court_colour   ... and its stroke recoloured to this
  court_frame_w  the frame around the court picture (<use xlink:href="#X.." stroke="#44F" fill="none">,
                 no width given, i.e. 1 unit) gets stroke=court_colour and this stroke-width

fill="#44F" (the blue robes) stays. Pip cards and aces have no #44F strokes, so only their index
changes. The output renders pixel-identically to tools/crisplab/crisplab.py prep_svg() with the same
parameters (the crispness lab's 'art' candidates; docs/DESIGN.md "Crispness decisions").
"""
import re
import sys


def edit(svg, index_stroke, court_mult, court_colour, court_frame_w):
    svg = svg.replace('stroke-width="80"', 'stroke-width="%s"' % index_stroke)
    # A thicker rank stroke reaches past the glyph symbol's viewBox, which clips by default and
    # shaved ~1 px off the round tops/bottoms of 0, 3, 6, 8, 9, Q... Let the rank symbols overflow.
    svg = re.sub(r'<symbol (id="V[^"]*")', r'<symbol overflow="visible" \1', svg)

    def tag(m):
        t = m.group(0)
        if 'stroke="#44F"' not in t:
            return t
        if 'xlink:href="#X' in t:   # the frame around the court picture (default width 1)
            return t.replace('stroke="#44F"', 'stroke="%s" stroke-width="%s"' % (court_colour, court_frame_w))
        t = re.sub(r'stroke-width="([0-9.]+)"',
                   lambda w: 'stroke-width="%.4g"' % (float(w.group(1)) * float(court_mult)), t)
        return t.replace('stroke="#44F"', 'stroke="%s"' % court_colour)

    return re.sub(r'<(?:path|use)\b[^>]*>', tag, svg)


def main(argv):
    if len(argv) != 7:
        sys.stderr.write(__doc__.split('\n\n')[1] + '\n')
        return 2
    src, dst, index_stroke, court_mult, court_colour, court_frame_w = argv[1:7]
    with open(src, encoding='utf-8', newline='') as f:
        svg = f.read()
    with open(dst, 'w', encoding='utf-8', newline='') as f:
        f.write(edit(svg, index_stroke, court_mult, court_colour, court_frame_w))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
