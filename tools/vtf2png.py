"""Minimal VTF -> PNG converter (top mip only).

Written for the HL2SB GMod port: GMod content only ships .vtf, and this engine's
most reliable material path is a raw .png (materialsystem/hl2sb_pngtexture.cpp),
so textures borrowed from GMod get converted once and committed/ignored as PNG.

Usage: python vtf2png.py <in.vtf> <out.png> [--dump]

The VTF header layout differs between versions (7.0 ... 7.5), so instead of
hard-coding offsets the file's own size is used to pin the layout down: the
header the mip chain the (optional) low-res thumbnail must add up to the file
size exactly.
"""

import struct
import sys
import zlib

FORMAT_SIZES = {
    # image format -> bytes per pixel for uncompressed formats
    0: 4,    # RGBA8888
    1: 4,    # ABGR8888
    2: 3,    # RGB888
    3: 3,    # BGR888
    4: 2,    # RGB565
    5: 1,    # I8
    6: 2,    # IA88
    7: 1,    # P8
    8: 1,    # A8
    9: 3,    # RGB888_BLUESCREEN
    10: 3,   # BGR888_BLUESCREEN
    11: 4,   # ARGB8888
    12: 4,   # BGRA8888
    13: None,  # DXT1
    14: None,  # DXT3
    15: None,  # DXT5
}
DXT_FORMATS = (13, 14, 15)
KNOWN_FORMATS = set(FORMAT_SIZES)


def mip_size(width, height, fmt):
    """Bytes of one mip level (uncompressed or DXT)."""
    if fmt in DXT_FORMATS:
        block = 8 if fmt == 13 else 16
        bw = max(1, (width + 3) // 4)
        bh = max(1, (height + 3) // 4)
        return bw * bh * block
    return width * height * FORMAT_SIZES[fmt]


def mip_chain_size(width, height, fmt, mip_count):
    total = 0
    w, h = width, height
    for _ in range(max(1, mip_count)):
        total += mip_size(w, h, fmt)
        w, h = max(1, w // 2), max(1, h // 2)
    return total


def is_pot(n):
    return n >= 1 and (n & (n - 1)) == 0


def find_layout(data):
    """Return every (header_size, width, height, fmt, mip_count, low_res_size) that
    adds up to the file size."""
    size = len(data)
    candidates = []

    for header_size in range(32, 1024 // 4):
        for w_off in range(8, min(header_size, 40) - 1):
            width = struct.unpack_from("<H", data, w_off)[0]
            height = struct.unpack_from("<H", data, w_off + 2)[0]
            if not (is_pot(width) and is_pot(height)) or width > 4096 or height > 4096:
                continue
            for f_off in range(w_off + 4, min(header_size, 96) - 3):
                fmt = struct.unpack_from("<I", data, f_off)[0]
                if fmt not in KNOWN_FORMATS:
                    continue
                for m_off in (f_off + 4, f_off + 5):
                    if m_off >= header_size:
                        continue
                    mip_count = data[m_off]
                    if not 1 <= mip_count <= 16:
                        continue
                    chain = mip_chain_size(width, height, fmt, mip_count)
                    if header_size + chain == size:
                        candidates.append((header_size, width, height, fmt, mip_count, 0, m_off))
                    # VTF 7.0/7.1 also carry a low-res DXT1 thumbnail.
                    for low_res_size in (128, 256, 512, 1024):
                        if header_size + chain + low_res_size == size:
                            candidates.append(
                                (header_size, width, height, fmt, mip_count, low_res_size, m_off)
                            )
    return candidates


def dxt1_block_colors(b0, b1):
    def rgb(c):
        r = (c >> 11) & 0x1F
        g = (c >> 5) & 0x3F
        b = c & 0x1F
        return ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))

    c0, c1 = rgb(b0), rgb(b1)
    if b0 > b1:
        c2 = tuple((2 * c0[i] + c1[i]) // 3 for i in range(3))
        c3 = tuple((c0[i] + 2 * c1[i]) // 3 for i in range(3))
        a2 = a3 = 255
    else:
        c2 = tuple((c0[i] + c1[i]) // 2 for i in range(3))
        c3 = (0, 0, 0)
        a2 = 255
        a3 = 0
    return ((c0, 255), (c1, 255), (c2, a2), (c3, a3))


def decode_dxt(data, width, height, fmt):
    """Decode DXT1/DXT3/DXT5 to RGBA bytes."""
    out = bytearray(width * height * 4)
    block = 8 if fmt == 13 else 16
    bw, bh = (width + 3) // 4, (height + 3) // 4
    pos = 0
    for by in range(bh):
        for bx in range(bw):
            chunk = data[pos : pos + block]
            pos += block
            if fmt == 13:
                alpha = None
                c0, c1 = struct.unpack_from("<HH", chunk, 0)
                bits = struct.unpack_from("<I", chunk, 4)[0]
                colors = dxt1_block_colors(c0, c1)
            elif fmt == 14:
                a = struct.unpack_from("<Q", chunk, 0)[0]
                alpha = [(a >> (4 * i)) & 0xF for i in range(16)]
                c0, c1 = struct.unpack_from("<HH", chunk, 8)
                bits = struct.unpack_from("<I", chunk, 12)[0]
                colors = dxt1_block_colors(c0, c1)  # always 4-colour mode for DXT3
                if c0 <= c1:
                    c2 = tuple((colors[0][0][i] + colors[1][0][i]) // 2 for i in range(3))
                    colors = (colors[0], colors[1], (c2, 255), colors[1])
            else:
                a0, a1 = chunk[0], chunk[1]
                abits = int.from_bytes(chunk[2:8], "little")
                c0, c1 = struct.unpack_from("<HH", chunk, 8)
                bits = struct.unpack_from("<I", chunk, 12)[0]
                colors = dxt1_block_colors(c0, c1)
                if c0 <= c1:
                    c2 = tuple((colors[0][0][i] + colors[1][0][i]) // 2 for i in range(3))
                    colors = (colors[0], colors[1], (c2, 255), (0, 0, 0))
                alpha = []
                for i in range(16):
                    code = (abits >> (3 * i)) & 0x7
                    if code == 0:
                        alpha.append(a0)
                    elif code == 1:
                        alpha.append(a1)
                    elif a0 > a1:
                        alpha.append(((8 - code) * a0 + (code - 1) * a1) // 7)
                    else:
                        if code == 6:
                            alpha.append(0)
                        elif code == 7:
                            alpha.append(255)
                        else:
                            alpha.append(((6 - code) * a0 + (code - 1) * a1) // 5)
            for py in range(4):
                for px in range(4):
                    x, y = bx * 4 + px, by * 4 + py
                    if x >= width or y >= height:
                        continue
                    idx = (py * 4 + px)
                    code = (bits >> (2 * idx)) & 0x3
                    (r, g, b), a = colors[code]
                    if alpha is not None:
                        a = alpha[idx]
                    o = (y * width + x) * 4
                    out[o : o + 4] = bytes((r, g, b, a))
    return bytes(out)


def decode_uncompressed(data, width, height, fmt):
    bpp = FORMAT_SIZES[fmt]
    out = bytearray(width * height * 4)
    for i in range(width * height):
        px = data[i * bpp : (i + 1) * bpp]
        if fmt in (0, 11):      # RGBA8888 / ARGB8888
            r, g, b, a = (px[0], px[1], px[2], px[3]) if fmt == 0 else (px[1], px[2], px[3], px[0])
        elif fmt in (1, 12):    # ABGR8888 / BGRA8888
            r, g, b, a = (px[3], px[2], px[1], px[0]) if fmt == 1 else (px[2], px[1], px[0], px[3])
        elif fmt == 2:
            r, g, b, a = px[0], px[1], px[2], 255
        elif fmt == 3:
            r, g, b, a = px[2], px[1], px[0], 255
        elif fmt == 5:
            r = g = b = px[0]
            a = 255
        elif fmt == 6:
            r = g = b = px[0]
            a = px[1]
        elif fmt == 8:
            r = g = b = 255
            a = px[0]
        else:
            raise SystemExit("unsupported uncompressed format %d" % fmt)
        out[i * 4 : i * 4 + 4] = bytes((r, g, b, a))
    return bytes(out)


def write_png(path, width, height, rgba):
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        raw += rgba[y * width * 4 : (y + 1) * width * 4]

    def chunk(tag, payload):
        return (
            struct.pack(">I", len(payload))
            + tag
            + payload
            + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)
        )

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def describe(rgba):
    opaque = sum(1 for i in range(3, len(rgba), 4) if rgba[i] > 8)
    return opaque / max(1, (len(rgba) // 4))


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    src, dst = sys.argv[1], sys.argv[2]
    with open(src, "rb") as f:
        data = f.read()

    if data[:4] != b"VTF\0":
        raise SystemExit("%s: not a VTF (signature %r)" % (src, data[:4]))
    major, minor = struct.unpack_from("<HH", data, 4)
    print("VTF %d.%d, %d bytes" % (major, minor, len(data)))

    candidates = find_layout(data)
    if not candidates:
        raise SystemExit("no header layout adds up to the file size")
    # Prefer the largest image (mip0) and, among equals, the deepest header.
    candidates.sort(key=lambda c: (-(c[1] * c[2]), c[5], -c[0]))
    header_size, width, height, fmt, mip_count, low_res_size, m_off = candidates[0]
    for c in candidates[:5]:
        print("  layout: header=%d %dx%d fmt=%d mips=%d lowres=%d" % (c[0], c[1], c[2], c[3], c[4], c[5]))

    chain = mip_chain_size(width, height, fmt, mip_count)
    # VTF stores the low-res thumbnail first (7.0/7.1) and the mip chain from the
    # smallest mip to mip0, so mip0 sits at the very end of the high-res block.
    mip0_size = mip_size(width, height, fmt)
    starts = {
        "smallest-first": header_size + low_res_size + chain - mip0_size,
        "mip0-first": header_size + low_res_size,
    }

    for label, start in starts.items():
        blob = data[start : start + mip0_size]
        if len(blob) < mip0_size:
            continue
        rgba = (
            decode_dxt(blob, width, height, fmt)
            if fmt in DXT_FORMATS
            else decode_uncompressed(blob, width, height, fmt)
        )
        out = dst if label == "smallest-first" else dst.replace(".png", "_%s.png" % label)
        write_png(out, width, height, rgba)
        print("  %-15s -> %s (opaque %.2f)" % (label, out, describe(rgba)))


if __name__ == "__main__":
    main()
