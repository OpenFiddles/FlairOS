#!/usr/bin/env python3
"""Package AirStinUI.airp and NetService.airp system packages."""
import os, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "build-system")
os.makedirs(OUT, exist_ok=True)

MAGIC = 0x50524941
TX_MAGIC = 0x54505041
HEADER = struct.Struct("<IHHIIII12s")

def pack_system_airp(name, payload_tag):
    # Minimal valid AIRP v1 API-2 container carrying TX_MAGIC + metadata
    body = struct.pack("<II", TX_MAGIC, 1) + payload_tag.encode("ascii").ljust(56, b"\x00")
    hdr = HEADER.pack(MAGIC, 1, HEADER.size, 2, 0x40000, 0x40000, len(body), name.encode("ascii")[:11])
    out_path = os.path.join(OUT, f"{name}.airp")
    with open(out_path, "wb") as f:
        f.write(hdr + body)
    print(f"[OK] {out_path} ({len(hdr) + len(body)} bytes)")

if __name__ == "__main__":
    pack_system_airp("AirStinUI", "AirStinUI SystemUI + ControlCenter + Installer")
    pack_system_airp("NetService", "NetService WiFi + 5G Cellular + Power")
