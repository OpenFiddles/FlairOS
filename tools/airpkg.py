#!/usr/bin/env python3
"""FlairOS AIRP package tool.

AIRP is FlairOS's application package container. Version 2 carries a flag word,
an executable payload, a UTF-8 JSON manifest and a typed resource table, with
CRC-32 integrity over the header, the payload and every resource. Version 1
packages (header plus flat image) remain readable.

Usage:
  airpkg.py pack     IMAGE.bin OUT.airp --name NAME [--manifest FILE]
                     [--resource NAME=PATH[:TYPE] ...] [--arch i386|aarch64|x86_64|any]
                     [--flags system,desktop] [--api LEVEL]
  airpkg.py list     PACKAGE.airp
  airpkg.py info     PACKAGE.airp
  airpkg.py validate PACKAGE.airp
  airpkg.py extract  PACKAGE.airp DIRECTORY
  airpkg.py manifest PACKAGE.airp

The container is documented in include/airp.h and docs/airp/FORMAT.md. This
module is also imported by the build scripts, so the packer and the kernel
loader cannot drift apart silently: tests/airp_pkg_test.py re-implements the
critical checks independently.
"""
import argparse
import json
import os
import struct
import sys
import zlib

AIRP_MAGIC = 0x50524941
AIRP_VERSION_V1 = 1
AIRP_VERSION_V2 = 2
AIRP_API_LEVEL = 2
AIRP_HEADER_V1_SIZE = 36
AIRP_HEADER_V2_SIZE = 64
AIRP_RESOURCE_SIZE = 24
AIRP_RES_MAX = 64
AIRP_RES_NAME_MAX = 40
AIRP_IMAGE_MAX = 131072
TX_APP_BASE = 0x40000
TX_APP_MAGIC = 0x54505041
AIRP_ARCH_ANY = 0xFFFF

ARCH_IDS = {"i386": 0x0000, "x86_64": 0x0001, "aarch64": 0x0002, "any": AIRP_ARCH_ANY}
ARCH_NAMES = {v: k for k, v in ARCH_IDS.items()}

RESOURCE_TYPES = {
    "blob": 0, "manifest": 1, "theme": 2, "icon": 3, "font": 4,
    "sound": 5, "strings": 6, "ui": 7, "plugin": 8, "library": 9,
}
RESOURCE_TYPE_NAMES = {v: k for k, v in RESOURCE_TYPES.items()}

FLAGS = {"icon": 0x0001, "system": 0x0002, "desktop": 0x0004, "resources-only": 0x0008}

V1_HEADER = struct.Struct("<IHHIIII12s")
V2_HEADER = struct.Struct("<IHHIIIIIIIIIIIIII")
RES_ENTRY = struct.Struct("<IIIIHHI")
assert V1_HEADER.size == AIRP_HEADER_V1_SIZE
assert V2_HEADER.size == AIRP_HEADER_V2_SIZE
assert RES_ENTRY.size == AIRP_RESOURCE_SIZE


class AirpError(Exception):
    pass


def crc32(data):
    return zlib.crc32(data) & 0xFFFFFFFF


def read_header(raw):
    """Returns a dict describing either a v1 or v2 header."""
    if len(raw) < AIRP_HEADER_V1_SIZE:
        raise AirpError("file is smaller than an AIRP header")
    magic, version = struct.unpack_from("<IH", raw, 0)
    if magic != AIRP_MAGIC:
        raise AirpError("missing AIRP signature")
    if version == AIRP_VERSION_V1:
        (_, ver, hsz, api, base, entry, isz, name) = V1_HEADER.unpack_from(raw)
        return {
            "version": ver, "header_size": hsz, "api_level": api, "image_base": base,
            "entry": entry, "image_size": isz, "name": name.split(b"\0", 1)[0].decode("ascii", "replace"),
            "flags": 0, "arch": ARCH_IDS["i386"], "resource_count": 0,
            "manifest_off": 0, "manifest_len": 0, "resource_tab_off": 0,
            "resource_tab_len": 0, "header_crc": 0, "payload_crc": 0,
            "file_size": AIRP_HEADER_V1_SIZE + isz,
        }
    if version != AIRP_VERSION_V2:
        raise AirpError(f"unsupported AIRP version {version}")
    if len(raw) < AIRP_HEADER_V2_SIZE:
        raise AirpError("truncated version 2 header")
    (_, ver, hsz, api, base, entry, isz, flags, moff, mlen, rcount, toff, tlen,
     arch, hcrc, pcrc, fsize) = V2_HEADER.unpack_from(raw)
    return {
        "version": ver, "header_size": hsz, "api_level": api, "image_base": base,
        "entry": entry, "image_size": isz, "flags": flags, "manifest_off": moff,
        "manifest_len": mlen, "resource_count": rcount, "resource_tab_off": toff,
        "resource_tab_len": tlen, "arch": arch, "header_crc": hcrc,
        "payload_crc": pcrc, "file_size": fsize, "name": "",
    }


def validate(raw, expected_arch=None):
    """Fully validates a package. Returns (header, resources). Raises AirpError."""
    h = read_header(raw)
    if h["file_size"] > len(raw):
        raise AirpError(f"declared size {h['file_size']} exceeds file size {len(raw)}")
    resources_only = bool(h["flags"] & FLAGS["resources-only"])
    if resources_only:
        if h["image_size"] != 0 or h["entry"] != 0:
            raise AirpError("a component package must not declare an executable")
        if h["version"] != 2:
            raise AirpError("component packages require version 2")
    else:
        if h["image_base"] != TX_APP_BASE:
            raise AirpError(f"image base 0x{h['image_base']:X} is not the app slot")
        if not 8 <= h["image_size"] <= AIRP_IMAGE_MAX:
            raise AirpError(f"image size {h['image_size']} is out of range")
        payload = raw[AIRP_HEADER_V2_SIZE if h["version"] == 2 else AIRP_HEADER_V1_SIZE:]
        if len(payload) < h["image_size"]:
            raise AirpError("image is truncated")
        if struct.unpack_from("<I", payload, 0)[0] != TX_APP_MAGIC:
            raise AirpError("image is missing the TX_APP_MAGIC head")
        entry_abs = struct.unpack_from("<I", payload, 4)[0]
        if not TX_APP_BASE <= entry_abs < TX_APP_BASE + h["image_size"]:
            raise AirpError("entry address is outside the image")
    if h["version"] == 1:
        return h, []

    if h["header_size"] != AIRP_HEADER_V2_SIZE:
        raise AirpError("version 2 header size is wrong")
    header_bytes = bytearray(raw[:AIRP_HEADER_V2_SIZE])
    stored = struct.unpack_from("<I", header_bytes, 52)[0]
    header_bytes[52:56] = b"\0\0\0\0"
    if crc32(bytes(header_bytes)) != stored:
        raise AirpError("header checksum mismatch")
    body = raw[AIRP_HEADER_V2_SIZE:h["file_size"]]
    if crc32(body) != h["payload_crc"]:
        raise AirpError("payload checksum mismatch")
    if expected_arch and expected_arch != AIRP_ARCH_ANY:
        if h["arch"] not in (expected_arch, AIRP_ARCH_ANY):
            raise AirpError(
                f"package targets {ARCH_NAMES.get(h['arch'], h['arch'])}, "
                f"not {ARCH_NAMES.get(expected_arch, expected_arch)}")
    if h["manifest_len"]:
        end = h["manifest_off"] + h["manifest_len"]
        if end > h["file_size"]:
            raise AirpError("manifest is out of range")
        try:
            json.loads(raw[h["manifest_off"]:end].decode("utf-8"))
        except (ValueError, UnicodeDecodeError) as e:
            raise AirpError(f"manifest is not valid UTF-8 JSON: {e}")
    if h["resource_count"] > AIRP_RES_MAX:
        raise AirpError(f"too many resources ({h['resource_count']} > {AIRP_RES_MAX})")
    if h["resource_count"]:
        if h["resource_tab_len"] != h["resource_count"] * AIRP_RESOURCE_SIZE:
            raise AirpError("resource table length does not match the count")
        if h["resource_tab_off"] + h["resource_tab_len"] > h["file_size"]:
            raise AirpError("resource table is out of range")
    if h["resource_tab_off"] + h["resource_tab_len"] > h["file_size"]:
        raise AirpError("resource table is out of range")

    resources = []
    name_blob_start = h["resource_tab_off"] + h["resource_tab_len"]
    for i in range(h["resource_count"]):
        base = h["resource_tab_off"] + i * AIRP_RESOURCE_SIZE
        noff, doff, dlen, dcrc, rtype, rflags, _reserved = RES_ENTRY.unpack_from(raw, base)
        if noff < name_blob_start or noff >= h["file_size"]:
            raise AirpError(f"resource {i} name offset is out of range")
        end = raw.find(b"\0", noff, h["file_size"])
        if end < 0:
            raise AirpError(f"resource {i} name is not terminated")
        name = raw[noff:end].decode("utf-8", "replace")
        if doff + dlen > h["file_size"]:
            raise AirpError(f"resource {name!r} data is out of range")
        if crc32(raw[doff:doff + dlen]) != dcrc:
            raise AirpError(f"resource {name!r} checksum mismatch")
        resources.append({
            "name": name, "type": rtype, "type_name": RESOURCE_TYPE_NAMES.get(rtype, "?"),
            "flags": rflags, "data_off": doff, "data_len": dlen, "crc": dcrc,
        })
    return h, resources


def manifest_of(raw, h):
    if not h["manifest_len"]:
        return None
    return raw[h["manifest_off"]:h["manifest_off"] + h["manifest_len"]]


def parse_flags(text):
    value = 0
    for token in (text or "").split(","):
        token = token.strip()
        if not token:
            continue
        if token not in FLAGS:
            raise AirpError(f"unknown flag {token!r}; known: {', '.join(sorted(FLAGS))}")
        value |= FLAGS[token]
    return value


def parse_resource_specs(specs):
    """Parses NAME=PATH[:TYPE] specs. The type suffix is optional and defaults to
    a type inferred from the extension."""
    out = []
    for spec in specs or []:
        if "=" not in spec:
            raise AirpError(f"resource spec {spec!r} must be NAME=PATH[:TYPE]")
        name, rest = spec.split("=", 1)
        rtype = None
        # Only treat a trailing :word as a type when it names a known type.
        if ":" in rest:
            maybe_path, maybe_type = rest.rsplit(":", 1)
            if maybe_type in RESOURCE_TYPES:
                rest, rtype = maybe_path, RESOURCE_TYPES[maybe_type]
        if not name or len(name.encode("utf-8")) > AIRP_RES_NAME_MAX:
            raise AirpError(f"resource name {name!r} is empty or longer than {AIRP_RES_NAME_MAX} bytes")
        with open(rest, "rb") as f:
            data = f.read()
        out.append((name, rtype if rtype is not None else infer_type(rest), data))
    return out


def infer_type(path):
    ext = os.path.splitext(path)[1].lower()
    return {
        ".json": RESOURCE_TYPES["theme"], ".bmp": RESOURCE_TYPES["icon"],
        ".png": RESOURCE_TYPES["icon"], ".ttf": RESOURCE_TYPES["font"],
        ".wav": RESOURCE_TYPES["sound"], ".ini": RESOURCE_TYPES["strings"],
        ".ui": RESOURCE_TYPES["ui"], ".airp": RESOURCE_TYPES["plugin"],
    }.get(ext, RESOURCE_TYPES["blob"])


def pack(image, name, manifest_bytes=None, resources=None, arch="i386",
         flags=0, api=AIRP_API_LEVEL):
    """Builds a version 2 package. `resources` is a list of (name, type, data).

    Passing `image=None` with the "resources-only" flag builds a component
    package: a container with a manifest and resources but no executable. That
    is how FlairDesktop ships its declarative session definition."""
    resources_only = bool(flags & FLAGS["resources-only"])
    if resources_only:
        if image:
            raise AirpError("a resources-only package must not carry an executable")
        image = b""
        entry = 0
    else:
        if not image:
            raise AirpError("an application package needs an executable image")
        if not 8 <= len(image) <= AIRP_IMAGE_MAX:
            raise AirpError(f"image is {len(image)} bytes; expected 8..{AIRP_IMAGE_MAX}")
        magic, entry = struct.unpack_from("<II", image, 0)
        if magic != TX_APP_MAGIC:
            raise AirpError("image does not start with TX_APP_MAGIC; link it with userland/app.ld")
        if not TX_APP_BASE <= entry < TX_APP_BASE + len(image):
            raise AirpError("entry address is outside the image")
    if arch not in ARCH_IDS:
        raise AirpError(f"unknown architecture {arch!r}; known: {', '.join(ARCH_IDS)}")
    resources = resources or []
    if len(resources) > AIRP_RES_MAX:
        raise AirpError(f"{len(resources)} resources exceeds the limit of {AIRP_RES_MAX}")

    manifest_bytes = manifest_bytes or b""
    if manifest_bytes:
        try:
            json.loads(manifest_bytes.decode("utf-8"))
        except (ValueError, UnicodeDecodeError) as e:
            raise AirpError(f"manifest is not valid UTF-8 JSON: {e}")

    body = bytearray()
    body += image
    manifest_off = AIRP_HEADER_V2_SIZE + len(body)
    body += manifest_bytes

    res_table_off = AIRP_HEADER_V2_SIZE + len(body)
    table_len = len(resources) * AIRP_RESOURCE_SIZE
    body += b"\0" * table_len

    # Offsets are absolute file offsets, so they include the 64-byte header.
    name_offsets = []
    for rname, _rtype, _rdata in resources:
        name_offsets.append(AIRP_HEADER_V2_SIZE + len(body))
        body += rname.encode("utf-8") + b"\0"

    entries = []
    for (rname, rtype, rdata), noff in zip(resources, name_offsets):
        data_off = AIRP_HEADER_V2_SIZE + len(body)
        body += rdata
        entries.append((noff, data_off, len(rdata), crc32(rdata), rtype, 0x0001, 0))

    file_size = AIRP_HEADER_V2_SIZE + len(body)

    header = bytearray(V2_HEADER.pack(
        AIRP_MAGIC, AIRP_VERSION_V2, AIRP_HEADER_V2_SIZE, api, TX_APP_BASE, entry,
        len(image), flags, manifest_off, len(manifest_bytes), len(resources),
        res_table_off, table_len, ARCH_IDS[arch], 0, 0, file_size))
    struct.pack_into("<I", header, 52, crc32(bytes(header)))

    out = bytearray(header)
    out += body
    for i, entry_fields in enumerate(entries):
        struct.pack_into("<IIIIHHI", out, res_table_off + i * AIRP_RESOURCE_SIZE, *entry_fields)
    # The resource table was reserved as zeros and now holds real entries, so the
    # payload checksum must be computed over the finished body, not the reserved one.
    struct.pack_into("<I", out, 56, crc32(bytes(out[AIRP_HEADER_V2_SIZE:])))
    struct.pack_into("<I", out, 52, 0)
    struct.pack_into("<I", out, 52, crc32(bytes(out[:AIRP_HEADER_V2_SIZE])))
    return bytes(out)


def cmd_pack(args):
    resources_only = "resources-only" in (args.flags or "")
    if args.input == "-" and resources_only:
        image = b""
    else:
        with open(args.input, "rb") as f:
            image = f.read()
    manifest_bytes = b""
    if args.manifest:
        with open(args.manifest, "rb") as f:
            manifest_bytes = f.read()
    elif args.name:
        manifest_bytes = json.dumps(
            {"name": args.name, "version": args.version, "arch": args.arch},
            separators=(",", ":")).encode("utf-8")
    resources = parse_resource_specs(args.resource)
    raw = pack(image, args.name, manifest_bytes, resources, args.arch,
               parse_flags(args.flags), args.api)
    os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)
    with open(args.output, "wb") as f:
        f.write(raw)
    print(f"[OK] {args.name or os.path.basename(args.output)}: {len(raw)} bytes "
          f"({len(resources)} resources) -> {args.output}")


def cmd_list(args):
    raw = open(args.file, "rb").read()
    h, resources = validate(raw)
    print(f"{args.file}: AIRP v{h['version']} "
          f"({ARCH_NAMES.get(h['arch'], h['arch'])}, API {h['api_level']})")
    print(f"  executable: {h['image_size']} bytes at 0x{h['image_base']:X} entry 0x{h['entry']:X}")
    if h["manifest_len"]:
        print(f"  manifest  : {h['manifest_len']} bytes")
    if not resources:
        print("  resources : none")
        return
    print(f"  resources : {len(resources)}")
    for r in resources:
        print(f"    - {r['name']:<20} {r['type_name']:<9} {r['data_len']:>6} bytes")


def cmd_info(args):
    raw = open(args.file, "rb").read()
    h, resources = validate(raw)
    flags = [name for name, bit in FLAGS.items() if h["flags"] & bit]
    print(f"Name      : {h['name'] or '(from manifest)'}")
    print(f"Magic     : 0x{AIRP_MAGIC:08X} (OK)")
    print(f"Version   : v{h['version']} (API {h['api_level']}, header {h['header_size']}B)")
    print(f"Arch      : {ARCH_NAMES.get(h['arch'], h['arch'])}")
    print(f"Flags     : {', '.join(flags) if flags else 'none'}")
    print(f"Base/Entry: 0x{h['image_base']:08X} / 0x{h['entry']:08X}")
    print(f"Image Size: {h['image_size']} bytes")
    print(f"File Size : {h['file_size']} bytes")
    if h["version"] == 2:
        print(f"Header CRC: 0x{h['header_crc']:08X}")
        print(f"Payload CRC: 0x{h['payload_crc']:08X}")
        m = manifest_of(raw, h)
        if m:
            print(f"Manifest  : {m.decode('utf-8', 'replace')}")
    print(f"Resources : {len(resources)}")


def cmd_validate(args):
    raw = open(args.file, "rb").read()
    arch = ARCH_IDS[args.arch] if args.arch else None
    h, resources = validate(raw, arch)
    print(f"[OK] {args.file}: AIRP v{h['version']}, {len(resources)} resources, checksums valid")


def cmd_extract(args):
    raw = open(args.file, "rb").read()
    h, resources = validate(raw)
    os.makedirs(args.directory, exist_ok=True)
    if h["image_size"]:
        payload = raw[AIRP_HEADER_V2_SIZE if h["version"] == 2 else AIRP_HEADER_V1_SIZE:]
        with open(os.path.join(args.directory, "main.bin"), "wb") as f:
            f.write(payload[:h["image_size"]])
    m = manifest_of(raw, h)
    if m:
        with open(os.path.join(args.directory, "manifest.json"), "wb") as f:
            f.write(m)
    for r in resources:
        safe = os.path.basename(r["name"]) or "resource"
        with open(os.path.join(args.directory, safe), "wb") as f:
            f.write(raw[r["data_off"]:r["data_off"] + r["data_len"]])
    print(f"[OK] extracted {h['image_size']} executable bytes and {len(resources)} resources "
          f"-> {args.directory}")


def cmd_manifest(args):
    raw = open(args.file, "rb").read()
    h, _ = validate(raw)
    m = manifest_of(raw, h)
    if not m:
        raise AirpError("package has no manifest")
    print(m.decode("utf-8", "replace"))


def main(argv=None):
    ap = argparse.ArgumentParser(prog="airpkg.py", description="FlairOS AIRP package tool")
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("pack", help="build a version 2 package from a linked image")
    p.add_argument("input", help="linked .bin image, or - for a resources-only package")
    p.add_argument("output")
    p.add_argument("--name", default="")
    p.add_argument("--version", default="1.0")
    p.add_argument("--manifest", default="")
    p.add_argument("--resource", action="append", default=[],
                   help="NAME=PATH[:TYPE]; repeatable")
    p.add_argument("--arch", default="i386", choices=sorted(ARCH_IDS))
    p.add_argument("--flags", default="")
    p.add_argument("--api", type=int, default=AIRP_API_LEVEL)
    p.set_defaults(func=cmd_pack)

    p = sub.add_parser("list", help="list package resources")
    p.add_argument("file")
    p.set_defaults(func=cmd_list)

    p = sub.add_parser("info", help="show package metadata")
    p.add_argument("file")
    p.set_defaults(func=cmd_info)

    p = sub.add_parser("validate", help="validate structure and checksums")
    p.add_argument("file")
    p.add_argument("--arch", default="", choices=[""] + sorted(ARCH_IDS))
    p.set_defaults(func=cmd_validate)

    p = sub.add_parser("extract", help="extract executable, manifest and resources")
    p.add_argument("file")
    p.add_argument("directory")
    p.set_defaults(func=cmd_extract)

    p = sub.add_parser("manifest", help="print the JSON manifest")
    p.add_argument("file")
    p.set_defaults(func=cmd_manifest)

    args = ap.parse_args(argv)
    try:
        args.func(args)
    except (AirpError, OSError) as e:
        sys.exit(f"airpkg: {e}")
    return 0


if __name__ == "__main__":
    main()
