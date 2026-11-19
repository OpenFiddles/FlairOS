#!/usr/bin/env python3
"""Validate built-in Settings and Notes AIRP containers."""
import pathlib
import struct
import sys

MAGIC = 0x50524941
TX_MAGIC = 0x54505041
HEADER = struct.Struct("<IHHIIII12s")
EXPECTED = {
    "settings": 32 * 512,
    "notes": 16 * 512,
}
if len(sys.argv) != 3:
    raise SystemExit("usage: airp_apps_test.py SETTINGS.airp NOTES.airp")
for path in map(pathlib.Path, sys.argv[1:]):
    raw = path.read_bytes()
    magic, version, header_size, api, base, entry, image_size, name = HEADER.unpack_from(raw)
    label = name.split(b"\\0", 1)[0].decode("ascii")
    assert label in EXPECTED, f"unexpected AIRP name: {label}"
    assert magic == MAGIC and version == 1 and header_size == HEADER.size
    assert api == 2, f"{label}: expected API level 2, got {api}"
    assert base == 0x40000 and 8 <= image_size <= 131072
    assert len(raw) == HEADER.size + image_size
    assert base <= entry < base + image_size
    assert struct.unpack_from("<I", raw, HEADER.size)[0] == TX_MAGIC
    assert len(raw) <= EXPECTED[label], f"{label}: exceeds reserved boot slot"
    print(f"{label}: AIRP v{version}, API {api}, {image_size} byte image OK")
