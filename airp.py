#!/usr/bin/env python3
"""FlairOS Single-File Python SDK CLI (airp.py).

Turns C projects, single .c files, ELF executables, or flat .bin images
into .airp packages linked against FlairOS's existing libc / SDK.

Usage:
  airp.py doctor
  airp.py init <dir> [name]
  airp.py clone <git_url> [dir] [--build] [--name NAME]
  airp.py build <dir_or_file> [--name NAME] [--out OUT.airp] [--arch i386|x86_64|aarch64] [--cflags FLAGS]
  airp.py pack <in.bin> <out.airp> <name>
  airp.py info <file.airp>
"""
import argparse
import glob
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.abspath(__file__))
SDK_DIR = os.path.join(ROOT, "sdk")
INC_DIR = os.path.join(ROOT, "include")
RT_C = os.path.join(ROOT, "userland", "rt.c")
STUBS_C = os.path.join(SDK_DIR, "flair_libc_stubs.c")
APP_LD = os.path.join(ROOT, "userland", "app.ld")

AIRP_MAGIC = 0x50524941
AIRP_VERSION = 1
AIRP_API_LEVEL = 2
TX_APP_BASE = 0x40000
TX_APP_MAGIC = 0x54505041
IMAGE_MAX = 262144
HEADER = struct.Struct("<IHHIIII12s")


def run(cmd, cwd=None):
    res = subprocess.run(cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if res.returncode != 0:
        if res.stdout:
            print(res.stdout.strip())
        raise SystemExit(f"airp: command failed ({res.returncode}): {' '.join(cmd)}")
    return res.stdout


def pack_bin(bin_path, out_path, name, raw_ok=False):
    with open(bin_path, "rb") as f:
        image = f.read()
    if not 8 <= len(image) <= IMAGE_MAX:
        raise SystemExit(f"airp: image is {len(image)} bytes; expected 8..{IMAGE_MAX}")
    magic, entry = struct.unpack_from("<II", image, 0)
    if magic != TX_APP_MAGIC:
        if not raw_ok:
            raise SystemExit("airp: .bin missing TX_APP_MAGIC (link with userland/app.ld or pass --raw)")
        entry = TX_APP_BASE
    elif not TX_APP_BASE <= entry < TX_APP_BASE + len(image):
        entry = TX_APP_BASE
    raw_name = name.encode("ascii", errors="ignore")[:11]
    if not raw_name:
        raise SystemExit("airp: invalid app name")
    hdr = HEADER.pack(
        AIRP_MAGIC, AIRP_VERSION, HEADER.size, AIRP_API_LEVEL,
        TX_APP_BASE, entry, len(image), raw_name.ljust(12, b"\0")
    )
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    with open(out_path, "wb") as f:
        f.write(hdr + image)
    print(f"[OK] {raw_name.decode('ascii')}: {HEADER.size + len(image)} bytes -> {out_path}")


def is_elf(path):
    with open(path, "rb") as f:
        return f.read(4) == b"\x7fELF"


def compile_c_project(sources, out_airp, name, arch="i386", extra_cflags=None, include_dirs=None):
    if arch != "i386":
        target = "x86_64-none-elf" if arch == "x86_64" else "aarch64-none-elf"
        cflags = [
            f"--target={target}", "-std=gnu99", "-ffreestanding", "-fPIC",
            "-fno-stack-protector", "-fno-builtin", "-fno-common", "-Os", "-Wall",
            "-DFLAIROS_SDK_APP", "-DFLAIROS_64BIT",
            f"-I{SDK_DIR}", f"-I{INC_DIR}",
        ]
    else:
        cflags = [
            "--target=i386-none-elf", "-m32", "-std=gnu99", "-ffreestanding",
            "-mno-sse", "-mno-sse2", "-mno-mmx", "-fno-pic", "-fno-stack-protector",
            "-fno-builtin", "-fno-common", "-Os", "-Wall",
            "-DFLAIROS_SDK_APP",
            f"-I{SDK_DIR}", f"-I{INC_DIR}",
        ]
    for inc in (include_dirs or []):
        cflags.append(f"-I{inc}")
    if extra_cflags:
        cflags.extend(extra_cflags)

    with tempfile.TemporaryDirectory(prefix="airp_build_") as tmp:
        objs = []
        all_c = list(sources)
        if arch == "i386":
            has_head = any((".head" in open(sc, errors="ignore").read() or "TX_APP_MAGIC" in open(sc, errors="ignore").read())
                           for sc in sources if os.path.exists(sc))
            if not has_head:
                shim = os.path.join(tmp, "_airp_entry_shim.c")
                with open(shim, "w") as sf:
                    sf.write(
                        'extern int main(void);\n'
                        'static void _airp_sys_exit(void) {\n'
                        '    __asm__ volatile ("int $0x80" : : "a"(1u), "b"(0u), "c"(0u), "d"(0u) : "memory");\n'
                        '}\n'
                        'void app_main(void) { (void)main(); _airp_sys_exit(); for (;;) {} }\n'
                        '__attribute__((section(".head"), used))\n'
                        'const unsigned int _airp_hdr[2] = { 0x54505041u, (unsigned int)(unsigned long)app_main };\n'
                    )
                all_c.append(shim)
            if os.path.exists(RT_C):
                all_c.append(RT_C)
        if os.path.exists(STUBS_C):
            all_c.append(STUBS_C)

        for idx, src in enumerate(all_c):
            obj = os.path.join(tmp, f"obj_{idx}_{os.path.basename(src)}.o")
            run(["clang"] + cflags + ["-c", os.path.abspath(src), "-o", obj])
            objs.append(obj)

        elf = os.path.join(tmp, f"{name}.elf")
        bin_p = os.path.join(tmp, f"{name}.bin")
        if arch == "i386":
            run(["ld.lld", "-m", "elf_i386", "-nostdlib", "-T", APP_LD, "-o", elf] + objs)
            run(["llvm-objcopy", "-O", "binary", elf, bin_p])
            pack_bin(bin_p, out_airp, name)
        else:
            emul = "elf_x86_64" if arch == "x86_64" else "aarch64elf"
            run(["ld.lld", "-m", emul, "-nostdlib", "-shared", "-o", elf] + objs)
            pack_bin(elf, out_airp, name, raw_ok=True)


def cmd_doctor(_args):
    tools = ["python3", "git", "clang", "ld.lld", "llvm-objcopy"]
    ok = True
    for t in tools:
        p = shutil.which(t)
        print(f"  {t:14s}: {p if p else 'MISSING'}")
        if not p:
            ok = False
    for label, path in [("sdk/", SDK_DIR), ("rt.c", RT_C), ("libc_stubs", STUBS_C), ("app.ld", APP_LD)]:
        ex = os.path.exists(path)
        print(f"  {label:14s}: {'OK' if ex else 'MISSING'} ({path})")
        if not ex:
            ok = False
    if not ok:
        raise SystemExit("airp doctor: missing requirements")
    print("airp doctor: all requirements ready")


def cmd_init(args):
    d = os.path.abspath(args.dir)
    os.makedirs(d, exist_ok=True)
    name = (args.name or os.path.basename(d) or "hello")[:11]
    manifest = {
        "name": name,
        "arch": "i386",
        "sources": ["main.c"],
        "include_dirs": ["."],
        "cflags": [],
    }
    with open(os.path.join(d, "airp.json"), "w") as f:
        json.dump(manifest, f, indent=2)
    main_c = os.path.join(d, "main.c")
    if not os.path.exists(main_c):
        with open(main_c, "w") as f:
            f.write(
                '#include "flair_posix.h"\n\n'
                'int main(void) {\n'
                '    return 0;\n'
                '}\n'
            )
    print(f"[OK] Initialized FlairOS C project '{name}' in {d}")


def cmd_clone(args):
    dest = args.dir or os.path.basename(args.url.rstrip("/")).removesuffix(".git")
    dest = os.path.abspath(dest)
    if os.path.exists(dest):
        print(f"[INFO] Updating existing repo in {dest}")
        run(["git", "pull", "--ff-only"], cwd=dest)
    else:
        run(["git", "clone", "--depth", "1", args.url, dest])
    if args.build:
        bargs = argparse.Namespace(
            target=dest,
            name=args.name,
            out=None,
            arch=args.arch,
            cflags=None,
            raw=False,
        )
        cmd_build(bargs)


def cmd_build(args):
    target = os.path.abspath(args.target)
    if not os.path.exists(target):
        raise SystemExit(f"airp: target not found: {target}")

    extra_cflags = args.cflags.split() if args.cflags else []

    if os.path.isdir(target):
        mpath = os.path.join(target, "airp.json")
        if os.path.exists(mpath):
            cfg = json.load(open(mpath))
            name = (args.name or cfg.get("name") or os.path.basename(target))[:11]
            arch = args.arch or cfg.get("arch", "i386")
            srcs = [os.path.join(target, s) for s in cfg.get("sources", [])]
            incs = [os.path.join(target, i) for i in cfg.get("include_dirs", ["."])]
            extra_cflags = cfg.get("cflags", []) + extra_cflags
        else:
            name = (args.name or os.path.basename(target))[:11]
            arch = args.arch or "i386"
            srcs = sorted(glob.glob(os.path.join(target, "*.c")) + glob.glob(os.path.join(target, "src", "*.c")))
            incs = [target, os.path.join(target, "include")]
        if not srcs:
            raise SystemExit(f"airp: no .c sources found in {target}")
        out = args.out or os.path.join(target, f"{name}.airp")
        compile_c_project(srcs, out, name, arch=arch, extra_cflags=extra_cflags, include_dirs=incs)
        return

    # Single file: .c, .bin, or ELF executable
    base = os.path.splitext(os.path.basename(target))[0]
    name = (args.name or base)[:11]
    out = args.out or os.path.join(os.path.dirname(target), f"{name}.airp")

    if target.endswith(".c"):
        compile_c_project([target], out, name, arch=args.arch or "i386", extra_cflags=extra_cflags,
                          include_dirs=[os.path.dirname(target)])
    elif target.endswith(".bin"):
        pack_bin(target, out, name, raw_ok=args.raw)
    elif is_elf(target):
        with tempfile.TemporaryDirectory(prefix="airp_elf_") as tmp:
            bin_p = os.path.join(tmp, f"{name}.bin")
            run(["llvm-objcopy", "-O", "binary", target, bin_p])
            pack_bin(bin_p, out, name, raw_ok=True)
    else:
        pack_bin(target, out, name, raw_ok=True)


def cmd_pack(args):
    pack_bin(args.input, args.output, args.name, raw_ok=args.raw)


def cmd_info(args):
    with open(args.file, "rb") as f:
        raw = f.read()
    if len(raw) < HEADER.size:
        raise SystemExit("airp: file too small")
    magic, ver, hsz, api, base, entry, isz, nm = HEADER.unpack_from(raw)
    label = nm.split(b"\0", 1)[0].decode("ascii", errors="replace")
    tx = struct.unpack_from("<I", raw, HEADER.size)[0] if len(raw) >= HEADER.size + 4 else 0
    print(f"Name      : {label}")
    print(f"Magic     : 0x{magic:08X} ({'OK' if magic == AIRP_MAGIC else 'BAD'})")
    print(f"Version   : v{ver} (API {api}, header {hsz}B)")
    print(f"Base/Entry: 0x{base:08X} / 0x{entry:08X}")
    print(f"Image Size: {isz} bytes (TX_MAGIC=0x{tx:08X})")


def main():
    ap = argparse.ArgumentParser(prog="airp.py", description="FlairOS Python SDK CLI")
    sub = ap.add_subparsers(dest="cmd", required=True)

    sub.add_parser("doctor", help="Check clang, ld.lld, llvm-objcopy, git, and sdk/")

    p_init = sub.add_parser("init", help="Create a starter FlairOS C project")
    p_init.add_argument("dir")
    p_init.add_argument("name", nargs="?")

    p_clone = sub.add_parser("clone", help="Clone a git repo and optionally build .airp")
    p_clone.add_argument("url")
    p_clone.add_argument("dir", nargs="?")
    p_clone.add_argument("--build", action="store_true")
    p_clone.add_argument("--name")
    p_clone.add_argument("--arch", default="i386", choices=["i386", "x86_64", "aarch64"])

    p_build = sub.add_parser("build", help="Compile C project / ELF / .bin into .airp")
    p_build.add_argument("target", nargs="?", default=".")
    p_build.add_argument("--name")
    p_build.add_argument("--out")
    p_build.add_argument("--arch", default=None, choices=["i386", "x86_64", "aarch64"])
    p_build.add_argument("--cflags")
    p_build.add_argument("--raw", action="store_true")

    p_pack = sub.add_parser("pack", help="Wrap a .bin image into .airp")
    p_pack.add_argument("input")
    p_pack.add_argument("output")
    p_pack.add_argument("name")
    p_pack.add_argument("--raw", action="store_true")

    p_info = sub.add_parser("info", help="Inspect an .airp package")
    p_info.add_argument("file")

    args = ap.parse_args()
    {
        "doctor": cmd_doctor,
        "init": cmd_init,
        "clone": cmd_clone,
        "build": cmd_build,
        "pack": cmd_pack,
        "info": cmd_info,
    }[args.cmd](args)


if __name__ == "__main__":
    main()
