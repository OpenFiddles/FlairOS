#!/usr/bin/env python3
"""Pancake (.pck): pack and manage FlareOS resource/cache bundles.

PCK is a data-only archive format. It is not an executable and never runs entries.
Usage:
  pancake.py pack OUT.pck FILE...
  pancake.py list BUNDLE.pck
  pancake.py verify BUNDLE.pck
  pancake.py extract BUNDLE.pck DIRECTORY
"""
import argparse
import os
from pathlib import Path
import struct
import sys
import zlib

MAGIC = b"PCK1"
VERSION = 1
HEADER = struct.Struct("<4sHHII")
ENTRY = struct.Struct("<32sQQI")
NAME_MAX = 31


def safe_name(raw):
    name = raw.replace("\\", "/")
    path = Path(name)
    if (not name or name.startswith("/") or path.is_absolute()
            or any(part in ("", ".", "..") for part in name.split("/"))
            or len(name.encode("utf-8")) > NAME_MAX):
        raise ValueError(f"invalid resource name: {raw!r}")
    return name


def read_bundle(path):
    data = Path(path).read_bytes()
    if len(data) < HEADER.size:
        raise ValueError("truncated PCK header")
    magic, version, count, index_size, data_offset = HEADER.unpack_from(data)
    if magic != MAGIC:
        raise ValueError("not a PCK bundle")
    if version != VERSION:
        raise ValueError(f"unsupported PCK version: {version}")
    if index_size != count * ENTRY.size:
        raise ValueError("invalid PCK index size")
    if data_offset != HEADER.size + index_size or data_offset > len(data):
        raise ValueError("invalid PCK data offset")
    entries = []
    names = set()
    ranges = []
    for i in range(count):
        at = HEADER.size + i * ENTRY.size
        raw_name, offset, size, checksum = ENTRY.unpack_from(data, at)
        try:
            name = raw_name.split(b"\\0", 1)[0].decode("utf-8")
            safe_name(name)
        except (UnicodeDecodeError, ValueError) as exc:
            raise ValueError(f"invalid entry {i}: {exc}") from exc
        if name in names:
            raise ValueError(f"duplicate entry: {name}")
        names.add(name)
        end = offset + size
        if offset < data_offset or end < offset or end > len(data):
            raise ValueError(f"entry outside archive: {name}")
        ranges.append((offset, end, name))
        entries.append((name, offset, size, checksum))
    ranges.sort()
    for previous, current in zip(ranges, ranges[1:]):
        if previous[1] > current[0]:
            raise ValueError(f"overlapping entries: {previous[2]} and {current[2]}")
    return data, entries


def pack(output, sources):
    items = []
    seen = set()
    for source in sources:
        path = Path(source)
        if not path.is_file():
            raise ValueError(f"not a file: {source}")
        name = safe_name(path.name)
        if name in seen:
            raise ValueError(f"duplicate resource name: {name}")
        seen.add(name)
        payload = path.read_bytes()
        items.append((name, payload, zlib.crc32(payload) & 0xFFFFFFFF))
    index_size = len(items) * ENTRY.size
    data_offset = HEADER.size + index_size
    cursor = data_offset
    index = bytearray()
    body = bytearray()
    for name, payload, checksum in items:
        raw_name = name.encode("utf-8").ljust(32, b"\\0")
        index += ENTRY.pack(raw_name, cursor, len(payload), checksum)
        body += payload
        cursor += len(payload)
    header = HEADER.pack(MAGIC, VERSION, len(items), index_size, data_offset)
    Path(output).write_bytes(header + index + body)


def main():
    parser = argparse.ArgumentParser(description="Pancake resource/cache bundle manager")
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("pack", help="create a .pck data bundle")
    p.add_argument("output")
    p.add_argument("files", nargs="+")
    for name in ("list", "verify"):
        q = sub.add_parser(name)
        q.add_argument("bundle")
    x = sub.add_parser("extract", help="extract data resources (never execute)")
    x.add_argument("bundle")
    x.add_argument("directory")
    args = parser.parse_args()
    try:
        if args.command == "pack":
            pack(args.output, args.files)
            print(f"pancake: packed {len(args.files)} resource(s) -> {args.output}")
            return
        data, entries = read_bundle(args.bundle)
        if args.command == "list":
            for name, offset, size, checksum in entries:
                print(f"{name}  {size} bytes  crc32={checksum:08x}")
            print(f"{len(entries)} resource(s)")
        elif args.command == "verify":
            for name, offset, size, checksum in entries:
                actual = zlib.crc32(data[offset:offset + size]) & 0xFFFFFFFF
                if actual != checksum:
                    raise ValueError(f"checksum mismatch: {name}")
            print(f"pancake: verified {len(entries)} resource(s)")
        elif args.command == "extract":
            root = Path(args.directory).resolve()
            for name, offset, size, checksum in entries:
                target = (root / name).resolve()
                if os.path.commonpath((str(root), str(target))) != str(root):
                    raise ValueError(f"unsafe output path: {name}")
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data[offset:offset + size])
            print(f"pancake: extracted {len(entries)} resource(s) to {root}")
    except (OSError, ValueError) as exc:
        print(f"pancake: error: {exc}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
