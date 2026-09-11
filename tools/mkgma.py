#!/usr/bin/env python3
# ===========================================================================
# tools/mkgma.py - pack a folder into a Garry's Mod .gma addon archive (v3).
#
#   python tools/mkgma.py <folder> <out.gma> [name] [author]
#
# Produces the layout HL2SB's game/shared/hl2sb/hl2sb_gma.cpp reads, all
# little-endian:
#
#   char[4]  magic  = "GMAD"
#   uint8    version            (3)
#   uint64   steamid            (v3)
#   uint64   timestamp          (v3)
#   cstring  requiredcontent    (empty)
#   cstring  name
#   cstring  description        (empty)
#   cstring  author
#   int32    addonversion       (v3)
#   repeated until filenumber == 0:
#       uint32   filenumber     (1-based)
#       cstring  filename       (relative, forward slashes)
#       int64    filesize
#       uint32   crc            (standard CRC-32, zlib.crc32)
#   then the raw bytes of every indexed file, in index order.
#
# A trailing "file number lookup" table is NOT written; it is optional and
# HL2SB ignores it.
# ===========================================================================

import os
import struct
import sys
import time
import zlib

MAGIC = b"GMAD"
VERSION = 3


def cstr(value):
    """NUL-terminated bytes for a GMA cstring field."""
    if isinstance(value, str):
        value = value.encode("utf-8")
    return value + b"\x00"


def collect_files(folder):
    """[(relative_path_with_forward_slashes, absolute_path)], sorted."""
    base = os.path.abspath(folder)
    found = []
    for root, dirs, files in os.walk(base):
        dirs.sort()
        for name in sorted(files):
            full = os.path.join(root, name)
            rel = os.path.relpath(full, base).replace(os.sep, "/")
            found.append((rel, full))
    found.sort(key=lambda item: item[0].lower())
    return found


def build(folder, out_path, name=None, author=""):
    if not os.path.isdir(folder):
        raise SystemExit("error: '%s' is not a directory" % folder)

    if name is None:
        name = os.path.basename(os.path.abspath(folder))

    files = collect_files(folder)
    if not files:
        print("warning: '%s' contains no files; writing an empty archive" % folder)

    index = bytearray()
    blobs = []
    for number, (rel, full) in enumerate(files, start=1):
        with open(full, "rb") as handle:
            data = handle.read()

        index += struct.pack("<I", number)
        index += cstr(rel)
        index += struct.pack("<q", len(data))
        index += struct.pack("<I", zlib.crc32(data) & 0xFFFFFFFF)
        blobs.append(data)

    index += struct.pack("<I", 0)  # end of the file index

    header = bytearray()
    header += MAGIC
    header += struct.pack("<B", VERSION)
    header += struct.pack("<Q", 0)                  # steamid
    header += struct.pack("<Q", int(time.time()))   # timestamp
    header += cstr("")                              # requiredcontent
    header += cstr(name)
    header += cstr("")                              # description
    header += cstr(author)
    header += struct.pack("<i", 0)                  # addonversion

    with open(out_path, "wb") as handle:
        handle.write(header)
        handle.write(index)
        for data in blobs:
            handle.write(data)

    return header, index, files


def main(argv):
    if len(argv) < 3 or len(argv) > 5:
        print("usage: python tools/mkgma.py <folder> <out.gma> [name] [author]")
        return 2

    folder = argv[1]
    out_path = argv[2]
    name = argv[3] if len(argv) > 3 else None
    author = argv[4] if len(argv) > 4 else ""

    header, index, files = build(folder, out_path, name, author)

    with open(out_path, "rb") as handle:
        first = handle.read(64)

    total = os.path.getsize(out_path)
    print("wrote %s (%d bytes, %d file(s), name=%r, author=%r)"
          % (out_path, total, len(files), name or os.path.basename(os.path.abspath(folder)), author))
    for rel, _ in files:
        print("  %s" % rel)
    print("first 64 bytes: %s" % first[:64].hex(" "))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
