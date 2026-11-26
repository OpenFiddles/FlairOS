#!/usr/bin/env python3
"""AIRP packaging round-trip test, independent of the kernel loader.

Builds packages with tools/airpkg.py, validates them, and re-parses every byte
of the header and resource table by hand so a bug in the packer cannot hide
behind its own validator.
"""
import struct
import sys
import os
import tempfile
import zlib

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
import airpkg

TX_APP_BASE = 0x40000
TX_APP_MAGIC = 0x54505041
failures = []


def check(cond, what):
    print(("PASS " if cond else "FAIL ") + what)
    if not cond:
        failures.append(what)


def image(entry_offset=8, size=32):
    """A minimal image: TX magic, entry, then filler."""
    body = bytearray(size)
    struct.pack_into("<II", body, 0, TX_APP_MAGIC, TX_APP_BASE + entry_offset)
    for i in range(8, size):
        body[i] = i & 0xFF
    return bytes(body)


def main():
    tmp = tempfile.mkdtemp()
    theme = b'{"accent":"violet"}'
    icon = bytes(range(48))
    raw = airpkg.pack(
        image(), "desktop",
        manifest_bytes=b'{"name":"desktop","version":"2.0.0"}',
        resources=[("theme.json", airpkg.RESOURCE_TYPES["theme"], theme),
                   ("app.bmp", airpkg.RESOURCE_TYPES["icon"], icon)],
        arch="i386", flags=airpkg.FLAGS["desktop"] | airpkg.FLAGS["system"])

    h, resources = airpkg.validate(raw)
    check(h["version"] == 2, "packer writes version 2")
    check(h["arch"] == airpkg.ARCH_IDS["i386"], "architecture is recorded")
    check(h["flags"] & airpkg.FLAGS["desktop"], "desktop flag is recorded")
    check(len(resources) == 2, "both resources are listed")
    check(resources[0]["name"] == "theme.json" and resources[0]["data_len"] == len(theme),
          "resource order and length are preserved")

    # Independent header parse.
    magic, ver, hsz, api, base, entry, isz = struct.unpack_from("<IHHIIII", raw, 0)
    check(magic == airpkg.AIRP_MAGIC and ver == 2 and hsz == 64, "header magic/version/size")
    check(base == TX_APP_BASE and entry == TX_APP_BASE + 8, "header base and entry")
    check(isz == 32, "header image size")

    # Independent CRC checks.
    hdr = bytearray(raw[:64])
    stored = struct.unpack_from("<I", hdr, 52)[0]
    hdr[52:56] = b"\0\0\0\0"
    check(zlib.crc32(bytes(hdr)) & 0xFFFFFFFF == stored, "header CRC is reproducible")
    payload_crc = struct.unpack_from("<I", raw, 56)[0]
    check(zlib.crc32(raw[64:]) & 0xFFFFFFFF == payload_crc, "payload CRC covers the body")

    # Every resource byte and CRC.
    tab_off = struct.unpack_from("<I", raw, 40)[0]
    count = struct.unpack_from("<I", raw, 36)[0]
    for i in range(count):
        noff, doff, dlen, dcrc, rtype, rflags, _ = struct.unpack_from("<IIIIHHI", raw, tab_off + i * 24)
        name = raw[noff:raw.index(b"\0", noff)].decode()
        data = raw[doff:doff + dlen]
        check(zlib.crc32(data) & 0xFFFFFFFF == dcrc, f"resource {name} CRC is valid")
        check(name in ("theme.json", "app.bmp"), f"resource {name} name is intact")
        check(rflags & 0x0001, f"resource {name} is marked required")

    expected = {"theme.json": theme, "app.bmp": icon}
    for r in resources:
        check(raw[r["data_off"]:r["data_off"] + r["data_len"]] == expected[r["name"]],
              f"resource {r['name']} bytes round-trip")

    # Corruption detection at three layers.
    bad = bytearray(raw); bad[64 + 10] ^= 0xFF
    try:
        airpkg.validate(bytes(bad)); check(False, "payload corruption is detected")
    except airpkg.AirpError:
        check(True, "payload corruption is detected")

    bad = bytearray(raw); bad[20] ^= 0xFF
    try:
        airpkg.validate(bytes(bad)); check(False, "header corruption is detected")
    except airpkg.AirpError:
        check(True, "header corruption is detected")

    bad = bytearray(raw)
    doff = resources[1]["data_off"]
    bad[doff] ^= 0xFF
    try:
        airpkg.validate(bytes(bad)); check(False, "resource corruption is detected")
    except airpkg.AirpError:
        check(True, "resource corruption is detected")

    # Arch filtering.
    try:
        airpkg.validate(raw, airpkg.ARCH_IDS["aarch64"])
        check(False, "architecture mismatch is rejected")
    except airpkg.AirpError:
        check(True, "architecture mismatch is rejected")

    # An image without the app head must be refused.
    try:
        airpkg.pack(b"\0" * 32, "bad", b"")
        check(False, "image without TX magic is refused")
    except airpkg.AirpError:
        check(True, "image without TX magic is refused")

    # Package tool CLI writes and reads a file.
    img = os.path.join(tmp, "main.bin")
    pkg = os.path.join(tmp, "test.airp")
    theme_path = os.path.join(tmp, "theme.json")
    with open(img, "wb") as f:
        f.write(image())
    with open(theme_path, "wb") as f:
        f.write(theme)
    rc = airpkg.main(["pack", img, pkg, "--name", "cli",
                      "--resource", f"theme.json={theme_path}:theme"])
    check(rc == 0 and os.path.exists(pkg), "CLI pack writes a package")
    with open(pkg, "rb") as f:
        check(airpkg.validate(f.read())[0]["version"] == 2, "CLI package is version 2")

    print(f"{'airp packaging: FAILURES' if failures else 'airp packaging: all checks passed'}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
