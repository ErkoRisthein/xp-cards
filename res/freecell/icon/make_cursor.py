#!/usr/bin/env python3
"""Build res/freecell/downarrow.cur: a 32x32 monochrome down arrow (white fill, black outline).

    make_cursor.py <out .cur>

Our own drawing for XP's "DownArrow" cursor (shown over a legal destination column): a 4-px shaft
and a broad head, rasterised from the geometry below with a rounded ~2-px outline. The hotspot is
the tip, (13,25), as in XP. Prints the bitmap as ASCII art ('#' black, 'o' white, '.' transparent).
"""
import struct, sys

SIZE, HOTSPOT = 32, (13, 25)
SHAFT_X, SHAFT_TOP, HEAD_Y = (12, 16), 2, 14     # pixel-edge coordinates
HEAD_X, TIP = (6.5, 21.5), (14, 25.2)

def white(x, y):
    cx, cy = x + 0.5, y + 0.5
    if SHAFT_X[0] <= cx <= SHAFT_X[1] and SHAFT_TOP <= cy <= HEAD_Y:
        return True
    if HEAD_Y <= cy <= TIP[1]:
        t = (cy - HEAD_Y) / (TIP[1] - HEAD_Y)
        return HEAD_X[0] + (TIP[0] - HEAD_X[0]) * t <= cx <= HEAD_X[1] + (TIP[0] - HEAD_X[1]) * t
    return False

def bitmap():
    w = [[white(x, y) for x in range(SIZE)] for y in range(SIZE)]
    def outline(x, y):          # within distance 2 of a white pixel
        return any(w[y + dy][x + dx] for dy in range(-2, 3) for dx in range(-2, 3)
                   if 0 <= y + dy < SIZE and 0 <= x + dx < SIZE and dx * dx + dy * dy <= 4)
    return [''.join('o' if w[y][x] else '#' if outline(x, y) else '.' for x in range(SIZE))
            for y in range(SIZE)]

def write_cur(path, rows):
    def plane(bit):             # 1-bpp rows, bottom-up, 4-byte aligned (32 px = 4 bytes)
        out = b''
        for r in reversed(rows):
            v = 0
            for c in r: v = v << 1 | bit(c)
            out += struct.pack('>I', v)
        return out
    xor = plane(lambda c: c == 'o')                 # 1 = white (palette entry 1)
    and_ = plane(lambda c: c == '.')                # 1 = transparent
    dib = (struct.pack('<IiiHHIIiiII', 40, SIZE, 2 * SIZE, 1, 1, 0, len(xor) + len(and_), 0, 0, 2, 0)
           + bytes((0, 0, 0, 0, 255, 255, 255, 0)) + xor + and_)
    head = struct.pack('<HHH', 0, 2, 1) + struct.pack('<BBBBHHII', SIZE, SIZE, 2, 0,
                                                      HOTSPOT[0], HOTSPOT[1], len(dib), 22)
    with open(path, 'wb') as f:
        f.write(head + dib)

if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    rows = bitmap()
    assert rows[HOTSPOT[1]][HOTSPOT[0]] == '#' and rows[HOTSPOT[1] + 1][HOTSPOT[0]] == '.'
    write_cur(sys.argv[1], rows)
    print('\n'.join(rows[:27]))
