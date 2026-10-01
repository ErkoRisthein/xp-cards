#!/usr/bin/env python3
"""Verify the structure of .ico/.cur files the way XP's loader sees them, and optionally decode
every image to a preview PNG.

    check_icocur.py <file.ico|file.cur> [preview.png]

Checks: ICONDIR header and type, directory entries consistent with each image's
BITMAPINFOHEADER (size, bpp, colour count, byte size, offsets inside the file, no overlap), no
PNG-compressed images (XP cannot read them), height = 2 x width (XOR + AND), BI_RGB only,
palette size, exact XOR/AND sizes. Prints one line per image; exits 1 on any problem.
"""
import struct, sys

def fail(msg):
    print('FAIL:', msg); sys.exit(1)

def main():
    data = open(sys.argv[1], 'rb').read()
    rsv, typ, n = struct.unpack_from('<HHH', data, 0)
    if rsv != 0 or typ not in (1, 2) or n == 0: fail('bad ICONDIR %d %d %d' % (rsv, typ, n))
    kind = 'icon' if typ == 1 else 'cursor'
    print('%s: %s, %d image(s), %d bytes' % (sys.argv[1], kind, n, len(data)))
    spans, decoded = [], []
    for i in range(n):
        w, h, ncol, rsv2, f1, f2, size, off = struct.unpack_from('<BBBBHHII', data, 6 + 16 * i)
        w, h = w or 256, h or 256
        if off + size > len(data) or off < 6 + 16 * n: fail('entry %d out of range' % i)
        spans.append((off, off + size))
        img = data[off:off + size]
        if img[:8] == b'\x89PNG\r\n\x1a\n': fail('entry %d is PNG-compressed' % i)
        (bsz, bw, bh, planes, bpp, comp, simg) = struct.unpack_from('<IiiHHII', img, 0)
        if bsz != 40: fail('entry %d: header size %d' % (i, bsz))
        if comp != 0: fail('entry %d: compression %d' % (i, comp))
        if (bw, bh) != (w, 2 * h): fail('entry %d: dib %dx%d vs dir %dx%d' % (i, bw, bh, w, h))
        if planes != 1: fail('entry %d: planes %d' % (i, planes))
        if typ == 1:
            if f2 != bpp: fail('entry %d: dir bpp %d vs dib %d' % (i, f2, bpp))
            if f1 != 1: fail('entry %d: dir planes %d' % (i, f1))
            if ncol != (1 << bpp if bpp < 8 else 0): fail('entry %d: colour count %d' % (i, ncol))
            hot = ''
        else:
            if not (0 <= f1 < w and 0 <= f2 < h): fail('entry %d: hotspot outside' % i)
            hot = ' hotspot (%d,%d)' % (f1, f2)
        clr_used = struct.unpack_from('<I', img, 32)[0]
        npal = (clr_used or (1 << bpp)) if bpp <= 8 else 0
        xor_stride = ((w * bpp + 31) // 32) * 4
        and_stride = ((w + 31) // 32) * 4
        need = 40 + 4 * npal + xor_stride * h + and_stride * h
        if need != size: fail('entry %d: size %d, expected %d' % (i, size, need))
        print('  #%d %2dx%-2d %2d bpp%s palette %d, %d bytes' % (i, w, h, bpp, hot, npal, size))
        pal = [struct.unpack_from('<4B', img, 40 + 4 * k) for k in range(npal)]
        xo = 40 + 4 * npal; ao = xo + xor_stride * h
        px = []
        for y in range(h):
            r = h - 1 - y
            row = []
            for x in range(w):
                m = img[ao + r * and_stride + x // 8] >> (7 - x % 8) & 1
                if bpp == 32:
                    b, g, rr, a = img[xo + r * xor_stride + 4 * x: xo + r * xor_stride + 4 * x + 4]
                    row.append((rr, g, b, a))
                    continue
                bit = x * bpp
                v = img[xo + r * xor_stride + bit // 8] >> (8 - bpp - bit % 8) & ((1 << bpp) - 1)
                b, g, rr, _ = pal[v]
                if m and (rr, g, b) == (0, 0, 0): row.append((0, 0, 0, 0))      # transparent
                elif m: row.append((255 - rr, 255 - g, 255 - b, 255))           # inverted (show as negative)
                else: row.append((rr, g, b, 255))
            px.append(row)
        decoded.append((w, h, bpp, px))
    spans.sort()
    for a, b in zip(spans, spans[1:]):
        if a[1] > b[0]: fail('images overlap')
    print('OK')
    if len(sys.argv) > 2:
        from PIL import Image
        z = 4 if max(d[0] for d in decoded) <= 48 else 1
        W = sum(d[0] * z + 8 for d in decoded) + 8
        H = max(d[1] for d in decoded) * z + 16
        sheet = Image.new('RGB', (W, H * 3), (255, 255, 255))
        for row, bg in enumerate(((255, 255, 255), (212, 208, 200), (0, 127, 0))):
            x = 8
            for w, h, bpp, p in decoded:
                im = Image.new('RGBA', (w, h))
                im.putdata([c for r in p for c in r])
                tile = Image.new('RGBA', (w, h), bg + (255,))
                tile.alpha_composite(im)
                sheet.paste(tile.convert('RGB').resize((w * z, h * z), Image.NEAREST), (x, row * H + 8))
                x += w * z + 8
        sheet.save(sys.argv[2])

main()
