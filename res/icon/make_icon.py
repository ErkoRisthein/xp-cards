#!/usr/bin/env python3
"""Build res/freecell.ico (XP-compatible) from our king art.

    make_icon.py <res/king/src/king_left.svg> <out dir for icon SVGs> <out .ico>

Design (in the spirit of XP FreeCell's icon, a king's head in front of a card): a white card with
red diamond pips, tilted behind the bust of our king looking left (outlines 1.3x bolder). The SVG
is written to <out dir>/icon_card.svg and rendered with rsvg-convert at 48, 32 and 24 px. At 16 px
the art turns to mush, so that size is an original pixel drawing of the king's head (PIXEL16).

Each size (48, 32, 24, 16) is stored three times, uncompressed BMP-style (no PNG entries: XP cannot
read them): 32 bpp with alpha, 8 bpp (adaptive palette) and 4 bpp (standard VGA palette), each with
a 1-bpp AND mask.
Requires Python 3 + Pillow and rsvg-convert on PATH.
"""
import io, re, struct, subprocess, sys
from PIL import Image

SIZES = (48, 32, 24, 16)
VGA16 = [(0, 0, 0), (128, 0, 0), (0, 128, 0), (128, 128, 0), (0, 0, 128), (128, 0, 128),
         (0, 128, 128), (192, 192, 192), (128, 128, 128), (255, 0, 0), (0, 255, 0), (255, 255, 0),
         (0, 0, 255), (255, 0, 255), (0, 255, 255), (255, 255, 255)]
DIAMOND = 'M-400 0C-350 0 0 -450 0 -500C0 -450 350 0 400 0C350 0 0 450 0 500C0 450 -350 0 -400 0Z'
SVG = '<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" viewBox="0 0 48 48" width="48" height="48">\n'

def king_parts(path):
    s = open(path, encoding='utf-8').read()
    defs = re.search(r'<defs>.*?</defs>', s, re.S).group(0)
    body = s[s.index('</defs>') + 7:s.rindex('</svg>')]
    return defs, body

def pip(x, y, size, rot):
    k = size / 1000.0
    return ('<path fill="#f00" transform="translate(%g %g) rotate(%g) scale(%g)" d="%s"/>'
            % (x, y, rot, k, DIAMOND))

def card_svg(defs, body):
    # the card: 5:7, tilted 12 degrees clockwise about its centre, behind the king
    card = ('<g transform="rotate(12 33 20)">'
            '<rect x="20.5" y="2.5" width="25" height="35" rx="2.4" fill="#fff" stroke="#303030" stroke-width="1"/>'
            + pip(41, 8, 6.5, 0) + pip(41, 20, 6.5, 0) + pip(41, 32, 6.5, 0) + '</g>')
    king = ('<svg x="-1" y="11" width="37" height="37" viewBox="300 -18 840 840" overflow="hidden">%s%s</svg>'
            % (defs, bold(body)))
    return SVG + '<title>FreeCell HD icon</title>\n' + card + '\n' + king + '\n</svg>\n'

def bold(body, k=1.3):
    """Thicker outlines so the bust survives icon sizes."""
    return re.sub(r'stroke-width="([\d.]+)"', lambda m: 'stroke-width="%g"' % (float(m.group(1)) * k), body)

# 16x16 head (looking left like the bust): K navy outline, k black, Y gold, R red, W white,
# w lavender (hair/beard texture), B blue, . transparent
PIXEL16 = """
..KYK.KYYK.KYK..
..KYYKYYYYKYYK..
..KYYYYRRYYYYK..
..KRRRRRRRRRRK..
...KkkkkkkkkK...
..KWWWWWWWwWwK..
..KWKKWKKWwWwWK.
..KWBWWBWWwWwWK.
..KWWKWWWWwWwWK.
..KWKKWWWWwWwWK.
..KKKWKKWWwWwWK.
..KWWRWWWwWwWK..
...KwWwWwWwWwK..
BBKRRKwKRKYRRKBB
BBKRRRKRRKYRRKBB
BBKRRRRRRKYRRKBB
"""
PIXEL_COLOURS = {'.': (0, 0, 0, 0), 'K': (0x22, 0x22, 0x9A, 255), 'k': (0, 0, 0, 255),
                 'Y': (0xFF, 0xCC, 0x44, 255), 'R': (255, 0, 0, 255), 'W': (255, 255, 255, 255),
                 'w': (0xC8, 0xC8, 0xEE, 255), 'B': (0x44, 0x44, 0xFF, 255)}

def pixel_art(text):
    rows = text.split()
    im = Image.new('RGBA', (len(rows[0]), len(rows)))
    im.putdata([PIXEL_COLOURS[c] for r in rows for c in r])
    return im

def render(svg, px):
    p = subprocess.run(['rsvg-convert', '-w', str(px), '-h', str(px)], input=svg.encode(),
                       capture_output=True, check=True)
    return Image.open(io.BytesIO(p.stdout)).convert('RGBA')

# ---- ICO writer ---------------------------------------------------------------------------------
def rows_bottom_up(h, row_bytes):
    return b''.join(row_bytes(y) for y in range(h - 1, -1, -1))

def pad4(b):
    return b + b'\0' * (-len(b) % 4)

def and_mask(im, opaque):
    w, h = im.size
    def row(y):
        bits = bytearray((w + 7) // 8)
        for x in range(w):
            if not opaque(x, y): bits[x // 8] |= 0x80 >> (x % 8)
        return pad4(bytes(bits))
    return rows_bottom_up(h, row)

def dib(im, bpp):
    """BITMAPINFOHEADER + palette + XOR + AND for one icon image."""
    w, h = im.size
    px = im.load()
    opaque = lambda x, y: px[x, y][3] >= 128
    if bpp == 32:
        palette = b''
        xor = rows_bottom_up(h, lambda y: b''.join(
            struct.pack('<4B', px[x, y][2], px[x, y][1], px[x, y][0], px[x, y][3]) if px[x, y][3]
            else b'\0\0\0\0' for x in range(w)))
        mask = and_mask(im, lambda x, y: px[x, y][3] > 0)
    else:
        rgb = Image.new('RGB', im.size)
        rgb.paste(im.convert('RGB'))
        if bpp == 4:
            pal = Image.new('P', (1, 1)); pal.putpalette(sum(VGA16, ()) + (0, 0, 0) * 240)
            # brighten a little first so anti-aliased whites map to white, not silver
            q = rgb.point(lambda v: min(255, v * 23 // 20)).quantize(palette=pal, dither=Image.Dither.NONE)
            colours = VGA16
        else:
            q = rgb.quantize(255, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
            p = q.getpalette()
            colours = [tuple(p[i:i + 3]) for i in range(0, min(len(p), 765) // 3 * 3, 3)]
            # index 0 must be black: transparent pixels are AND=1, XOR=0 (else XP inverts the screen)
            colours = [(0, 0, 0)] + colours
        colours = (colours + [(0, 0, 0)] * 256)[:1 << bpp]
        palette = b''.join(struct.pack('<4B', b, g, r, 0) for r, g, b in colours)
        qp = q.load()
        shift = 1 if bpp == 8 else 0
        idx = lambda x, y: qp[x, y] + shift if opaque(x, y) else 0
        if bpp == 8:
            xor = rows_bottom_up(h, lambda y: pad4(bytes(idx(x, y) for x in range(w))))
        else:
            xor = rows_bottom_up(h, lambda y: pad4(bytes(
                (idx(x, y) << 4) | (idx(x + 1, y) if x + 1 < w else 0) for x in range(0, w, 2))))
        mask = and_mask(im, opaque)
    hdr = struct.pack('<IiiHHIIiiII', 40, w, 2 * h, 1, bpp, 0, len(xor) + len(mask), 0, 0, 0, 0)
    return hdr + palette + xor + mask

def write_ico(path, images):
    """images: list of (PIL RGBA image, bpp)."""
    blobs = [dib(im, bpp) for im, bpp in images]
    out = struct.pack('<HHH', 0, 1, len(blobs))
    off = 6 + 16 * len(blobs)
    for (im, bpp), b in zip(images, blobs):
        w, h = im.size
        out += struct.pack('<BBBBHHII', w % 256, h % 256, 16 if bpp == 4 else 0, 0, 1, bpp, len(b), off)
        off += len(b)
    with open(path, 'wb') as f:
        f.write(out + b''.join(blobs))

if __name__ == '__main__':
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    defs, body = king_parts(sys.argv[1])
    svg = card_svg(defs, body)
    with open('%s/icon_card.svg' % sys.argv[2], 'w', encoding='utf-8') as f:
        f.write(svg)
    full = {s: render(svg, s) for s in SIZES if s != 16}
    full[16] = pixel_art(PIXEL16)
    images = [(full[s], bpp) for bpp in (32, 8, 4) for s in SIZES]
    write_ico(sys.argv[3], images)
