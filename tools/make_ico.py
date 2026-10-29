#!/usr/bin/env python3
"""Generates Diary.ico: the same red leather diary as the classic Mac
version's icon, as 32x32 and 16x16 images with 16 colours (which every
Windows 95/98 display can show). Run:  python3 tools/make_ico.py Diary.ico
"""

import struct
import sys

# The icon's own 16-colour palette; index 0 is unused (transparent pixels
# are cut out by the mask).
COLOURS = {
    'K': (0x00, 0x00, 0x00),    # black outline
    'R': (0xCC, 0x00, 0x00),    # red leather cover
    'H': (0xFF, 0x66, 0x66),    # cover highlight
    'D': (0x66, 0x00, 0x00),    # dark red spine / strap
    'W': (0xFF, 0xFF, 0xFF),    # page white
    'G': (0xCC, 0xCC, 0xCC),    # page edge lines
    'Y': (0xFF, 0xCC, 0x00),    # gold
    'O': (0xCC, 0x99, 0x00),    # dark gold
    'B': (0x00, 0x66, 0xFF),    # ribbon
}
KEYS = '.' + ''.join(COLOURS)


def new_grid(n):
    return [['.'] * n for _ in range(n)]


def rect(g, top, left, bottom, right, c):
    for y in range(top, bottom + 1):
        for x in range(left, right + 1):
            g[y][x] = c


def frame(g, top, left, bottom, right, c):
    for x in range(left, right + 1):
        g[top][x] = g[bottom][x] = c
    for y in range(top, bottom + 1):
        g[y][left] = g[y][right] = c


def large():
    g = new_grid(32)
    # Page block peeking out at the right and bottom
    rect(g, 3, 5, 29, 27, 'W')
    frame(g, 3, 5, 29, 27, 'K')
    for y in range(5, 28, 2):
        g[y][26] = 'G'
    for x in range(7, 26, 2):
        g[28][x] = 'G'
    # Cover
    rect(g, 1, 3, 27, 25, 'R')
    frame(g, 1, 3, 27, 25, 'K')
    for x in range(9, 25):
        g[2][x] = 'H'
    for y in range(2, 27):
        g[y][9] = 'H'
    # Spine
    rect(g, 2, 4, 26, 7, 'D')
    for x in range(4, 8):
        g[5][x] = g[23][x] = 'Y'
    for y in range(2, 27):
        g[y][8] = 'K'
    # Gold label with two text lines
    rect(g, 6, 12, 12, 22, 'W')
    frame(g, 6, 12, 12, 22, 'O')
    for x in range(14, 21):
        g[8][x] = 'G'
    for x in range(14, 19):
        g[10][x] = 'G'
    # Strap across the right edge with a gold clasp
    rect(g, 15, 19, 19, 27, 'D')
    frame(g, 15, 19, 19, 27, 'K')
    rect(g, 14, 20, 20, 23, 'Y')
    frame(g, 14, 20, 20, 23, 'K')
    g[17][21] = g[17][22] = 'O'
    # Ribbon bookmark with a notched end
    rect(g, 28, 11, 31, 13, 'B')
    g[31][12] = '.'
    for y in (30, 31):
        g[y][10] = g[y][14] = 'K'
    return g


def small():
    g = new_grid(16)
    rect(g, 2, 3, 15, 14, 'W')
    frame(g, 2, 3, 15, 14, 'K')
    rect(g, 0, 1, 13, 12, 'R')
    frame(g, 0, 1, 13, 12, 'K')
    rect(g, 1, 2, 12, 3, 'D')
    for y in range(1, 13):
        g[y][4] = 'K'
    rect(g, 3, 6, 5, 10, 'W')
    frame(g, 3, 6, 5, 10, 'O')
    rect(g, 7, 10, 9, 14, 'D')
    frame(g, 7, 10, 9, 14, 'K')
    rect(g, 7, 10, 9, 11, 'Y')
    g[14][7] = g[15][7] = 'B'
    return g


def image(g):
    """One icon image: BITMAPINFOHEADER, palette, 4-bit pixels, 1-bit mask,
    rows bottom-up and padded to 4 bytes."""
    n = len(g)
    palette = b''.join(bytes((b, gr, r, 0)) for r, gr, b in
                       [(0, 0, 0)] + list(COLOURS.values()) + [(0, 0, 0)] * (15 - len(COLOURS)))
    pixels = bytearray()
    mask = bytearray()
    for row in reversed(g):
        line = bytearray()
        for x in range(0, n, 2):
            line.append(KEYS.index(row[x]) << 4 | KEYS.index(row[x + 1]))
        pixels += line + bytes(-len(line) % 4)
        bits = bytearray()
        for x in range(0, n, 8):
            byte = 0
            for k in range(8):
                byte = byte << 1 | (row[x + k] == '.')
            bits.append(byte)
        mask += bits + bytes(-len(bits) % 4)
    info = struct.pack('<IiiHHIIiiII', 40, n, 2 * n, 1, 4, 0, len(pixels) + len(mask), 0, 0, 16, 0)
    return info + palette + bytes(pixels) + bytes(mask)


def main():
    images = [(32, image(large())), (16, image(small()))]
    out = struct.pack('<HHH', 0, 1, len(images))
    offset = 6 + 16 * len(images)
    for size, data in images:
        out += struct.pack('<BBBBHHII', size, size, 16, 0, 1, 4, len(data), offset)
        offset += len(data)
    out += b''.join(data for _, data in images)
    with open(sys.argv[1], 'wb') as f:
        f.write(out)


main()
