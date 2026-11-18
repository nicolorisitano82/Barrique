#!/usr/bin/env python3
"""Barrique Caption: the window caption glyphs Windows 10/11 programs draw from
the Segoe Fluent Icons / Segoe MDL2 Assets fonts, which Wine does not have
(Firefox's minimize, maximize, restore and close buttons, for example).

Original geometric outlines, drawn here; nothing comes from Microsoft's fonts.
prefix-defaults.reg maps both family names to this one through
HKCU\\Software\\Wine\\Fonts\\Replacements (GDI and DirectWrite read it).

    make-caption-font.py <output.ttf>

Plain Python, no font tools needed: writes a minimal TrueType file with the
head, hhea, maxp, OS/2, hmtx, cmap, loca, glyf, name and post tables.
"""
import struct
import sys

EM = 1000          # units per em; the icons fill the em square
THIN = 100         # stroke at 10px is 1px, like Segoe Fluent Icons
THICK = 150        # high contrast variants (prefers-contrast)


def area(poly):
    return sum(x0 * y1 - x1 * y0 for (x0, y0), (x1, y1) in zip(poly, poly[1:] + poly[:1])) / 2


def outer(poly):
    """TrueType fills to the right of the direction: outer contours clockwise."""
    return poly if area(poly) < 0 else poly[::-1]


def inner(poly):
    return poly if area(poly) > 0 else poly[::-1]


def rect(x0, y0, x1, y1):
    return [(x0, y0), (x0, y1), (x1, y1), (x1, y0)]


def frame(x0, y0, x1, y1, t):
    return [outer(rect(x0, y0, x1, y1)), inner(rect(x0 + t, y0 + t, x1 - t, y1 - t))]


def minimize(t):
    return [outer(rect(0, (EM - t) // 2, EM, (EM + t) // 2))]


def maximize(t):
    return frame(0, 0, EM, EM, t)


def restore(t):
    front = 4 * EM // 5
    back = EM - front                     # offset of the back window
    # the front window, and the part of the back window that shows above and to its right
    corner = [(back, front), (back, EM), (EM, EM), (EM, back), (front, back), (front, back + t),
              (EM - t, back + t), (EM - t, EM - t), (back + t, EM - t), (back + t, front)]
    return frame(0, 0, front, front, t) + [outer(corner)]


def close(t):
    d = round(t / 2 * 2 ** 0.5)           # half the stroke, along an axis
    a = [(0, d), (d, 0), (EM, EM - d), (EM - d, EM)]
    b = [(EM - x, y) for x, y in a]
    return [outer(a), outer(b)]           # overlapping contours: nonzero fill


GLYPHS = [  # (code points, contours); glyph 0 is .notdef (empty)
    ([0xE921], minimize(THIN)), ([0xE922], maximize(THIN)),
    ([0xE923], restore(THIN)), ([0xE8BB], close(THIN)),
    ([0xEF2D], minimize(THICK)), ([0xEF2E], maximize(THICK)),
    ([0xEF2F], restore(THICK)), ([0xEF2C], close(THICK)),
]


def glyph_data(contours):
    if not contours:
        return b'', (0, 0, 0, 0)
    pts = [p for c in contours for p in c]
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    bbox = (min(xs), min(ys), max(xs), max(ys))
    out = struct.pack('>hhhhh', len(contours), *bbox)
    end = -1
    for c in contours:
        end += len(c)
        out += struct.pack('>H', end)
    out += struct.pack('>H', 0)                       # no instructions
    out += bytes([0x01] * len(pts))                   # on-curve, 16-bit deltas
    px = py = 0
    dx, dy = b'', b''
    for x, y in pts:
        dx += struct.pack('>h', x - px)
        dy += struct.pack('>h', y - py)
        px, py = x, y
    out += dx + dy
    return out + b'\0' * (-len(out) % 4), bbox


def checksum(data):
    data += b'\0' * (-len(data) % 4)
    return sum(struct.unpack('>%dI' % (len(data) // 4), data)) & 0xffffffff


def build():
    glyphs = [([], [])] + GLYPHS
    datas = [glyph_data(c) for _, c in glyphs]
    glyf = b''.join(d for d, _ in datas)
    loca, off = [], 0
    for d, _ in datas:
        loca.append(off)
        off += len(d)
    loca.append(off)
    boxes = [b for d, b in datas if d]
    bbox = (min(b[0] for b in boxes), min(b[1] for b in boxes), max(b[2] for b in boxes), max(b[3] for b in boxes))
    max_points = max(sum(len(c) for c in cs) for _, cs in glyphs)
    max_contours = max(len(cs) for _, cs in glyphs)
    n = len(glyphs)

    head = struct.pack('>IIIIHHqqhhhhHHhhh', 0x00010000, 0x00010000, 0, 0x5F0F3CF5, 0x000B, EM, 0, 0,
                       *bbox, 0, 8, 2, 1, 0)
    hhea = struct.pack('>IhhhHhhhhhh4hhH', 0x00010000, EM, 0, 0, EM, 0, 0, EM, 1, 0, 0, 0, 0, 0, 0, 0, n)
    maxp = struct.pack('>IHHHHHHHHHHHHHH', 0x00010000, n, max_points, max_contours, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0)
    first = min(c for cs, _ in GLYPHS for c in cs)
    last = max(c for cs, _ in GLYPHS for c in cs)
    os2 = struct.pack('>HhHHHhhhhhhhhhhh10sIIII4sHHHhhhHHII', 4, EM, 400, 5, 0,
                      650, 700, 0, 140, 650, 700, 0, 480, 50, 250, 0, b'\0' * 10,
                      0, 1 << (60 - 32), 0, 0, b'BRRQ', 0x40, first, last,
                      EM, 0, 0, EM, 0, 1, 0)
    os2 += struct.pack('>hhHHH', 500, EM, 0, 0x20, 0)
    hmtx = b''.join(struct.pack('>Hh', EM, b[0] if d else 0) for d, b in datas)

    cmap_map = sorted((c, g + 1) for g, (cs, _) in enumerate(GLYPHS) for c in cs)
    segs = [(c, c, (g - c) & 0xffff) for c, g in cmap_map] + [(0xffff, 0xffff, 1)]
    seg_x2 = 2 * len(segs)
    sr = 2 * 2 ** (len(segs).bit_length() - 1)
    sub = struct.pack('>HHHHHHH', 4, 16 + 8 * len(segs), 0, seg_x2, sr, (sr // 2).bit_length() - 1, seg_x2 - sr)
    sub += b''.join(struct.pack('>H', e) for _, e, _ in segs) + b'\0\0'
    sub += b''.join(struct.pack('>H', s) for s, _, _ in segs)
    sub += b''.join(struct.pack('>H', d) for _, _, d in segs)
    sub += b'\0\0' * len(segs)
    cmap = struct.pack('>HHHHI', 0, 1, 3, 1, 12) + sub

    names = {0: 'Original outlines for Barrique, LGPL-2.1-or-later', 1: 'Barrique Caption', 2: 'Regular',
             3: 'Barrique Caption 1.0', 4: 'Barrique Caption', 5: 'Version 1.0', 6: 'BarriqueCaption'}
    strings, records = b'', b''
    for nid, s in names.items():
        u = s.encode('utf-16-be')
        records += struct.pack('>HHHHHH', 3, 1, 0x409, nid, len(u), len(strings))
        strings += u
    name = struct.pack('>HHH', 0, len(names), 6 + 12 * len(names)) + records + strings
    post = struct.pack('>IIhhIIIII', 0x00030000, 0, -100, 50, 0, 0, 0, 0, 0)

    tables = {b'OS/2': os2, b'cmap': cmap, b'glyf': glyf, b'head': head, b'hhea': hhea, b'hmtx': hmtx,
              b'loca': b''.join(struct.pack('>I', o) for o in loca), b'maxp': maxp, b'name': name, b'post': post}
    tags = sorted(tables)
    sr = 16 * 2 ** (len(tags).bit_length() - 1)
    font = struct.pack('>IHHHH', 0x00010000, len(tags), sr, (sr // 16).bit_length() - 1, 16 * len(tags) - sr)
    offset = 12 + 16 * len(tags)
    body = b''
    for tag in tags:
        data = tables[tag]
        font += struct.pack('>4sIII', tag, checksum(data), offset + len(body), len(data))
        body += data + b'\0' * (-len(data) % 4)
    font += body
    adjust = (0xB1B0AFBA - checksum(font)) & 0xffffffff
    head_off = offset + sum(len(tables[t]) + (-len(tables[t]) % 4) for t in tags[:tags.index(b'head')])
    return font[:head_off + 8] + struct.pack('>I', adjust) + font[head_off + 12:]


if __name__ == '__main__':
    with open(sys.argv[1], 'wb') as f:
        f.write(build())
