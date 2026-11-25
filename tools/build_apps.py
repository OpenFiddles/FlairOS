#!/usr/bin/env python3
"""Wildcard builder for all apps/*/ projects -> build-apps/*.airp + include/vfs_apps_gen.h."""
import glob
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(ROOT, "build-apps")
GEN_HDR = os.path.join(ROOT, "include", "vfs_apps_gen.h")
AIRP_PY = os.path.join(ROOT, "airp.py")


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    app_dirs = sorted(d for d in glob.glob(os.path.join(ROOT, "apps", "*")) if os.path.isdir(d))
    built = []

    for d in app_dirs:
        name = os.path.basename(d)
        # Build i386 .airp + aarch64 .airp into build-apps/
        out_32 = os.path.join(OUT_DIR, f"{name}.airp")
        out_64 = os.path.join(OUT_DIR, f"{name}_64.airp")
        subprocess.check_call([sys.executable, AIRP_PY, "build", d, "--out", out_32], cwd=ROOT)
        subprocess.check_call([sys.executable, AIRP_PY, "build", d, "--arch", "aarch64", "--out", out_64], cwd=ROOT)
        built.append((name, out_32))

    # Check Downloads for any user-provided browser .apk
    dl_apks = sorted(
        glob.glob("/storage/shared/Downloads/*.apk") +
        glob.glob(os.path.expanduser("~/storage/shared/Downloads/*.apk"))
    )

    lines = [
        "#ifndef FLAIROS_VFS_APPS_GEN_H",
        "#define FLAIROS_VFS_APPS_GEN_H",
        "#include <stdint.h>",
        "",
    ]
    for idx, (name, path) in enumerate(built):
        raw = open(path, "rb").read()
        sym = f"gen_app_{idx}_bytes"
        lines.append(f"static const uint8_t {sym}[] = {{")
        for off in range(0, len(raw), 16):
            chunk = ", ".join(f"0x{b:02X}u" for b in raw[off:off + 16])
            lines.append(f"    {chunk},")
        lines.append("};")
    lines.append("")
    lines.append("static inline void vfs_seed_generated_apps(void (*seed_fn)(const char *, const uint8_t *, uint32_t)) {")
    for idx, (name, _) in enumerate(built):
        sym = f"gen_app_{idx}_bytes"
        lines.append(f'    seed_fn("/apps/{name}.airp", {sym}, (uint32_t)sizeof({sym}));')
    lines.append("}")
    lines.append("#endif")

    with open(GEN_HDR, "w") as f:
        f.write("\n".join(lines) + "\n")

    print(f"[OK] Wildcard built {len(built)} apps/* projects -> {OUT_DIR} & {GEN_HDR}")
    if dl_apks:
        print(f"[INFO] Found Downloads APKs: {', '.join(dl_apks)}")
    else:
        print("[INFO] No .apk found in /storage/shared/Downloads yet.")


if __name__ == "__main__":
    main()
