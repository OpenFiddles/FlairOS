#!/usr/bin/env python3
"""Build /ported/developer/ apps (toybox, kilo) against FlairOS SDK
   and package them with airpack.py (.airp) and pancake.py (.pck).
"""
import os, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "build-ported")
os.makedirs(OUT, exist_ok=True)

def run(cmd):
    r = subprocess.run(cmd, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if r.returncode != 0:
        print(r.stdout)
        raise RuntimeError(f"Failed ({r.returncode}): {' '.join(cmd)}")
    return r.stdout

def build_sdk_airp(name, src_rel, extra_files):
    obj = os.path.join(OUT, f"{name}.o")
    rt_obj = os.path.join(OUT, "rt.o")
    elf = os.path.join(OUT, f"{name}.elf")
    bin_p = os.path.join(OUT, f"{name}.bin")
    airp = os.path.join(OUT, f"{name}.airp")
    pck = os.path.join(ROOT, "ported/developer", name, f"{name}.pck")

    cflags = [
        "--target=i386-none-elf", "-m32", "-std=gnu99", "-ffreestanding",
        "-mno-sse", "-mno-sse2", "-mno-mmx", "-fno-pic", "-fno-stack-protector",
        "-fno-builtin", "-fno-common", "-Os", "-Wall", "-Wextra",
        "-DFLAIROS_SDK_APP", f"-I{ROOT}/sdk", f"-I{ROOT}/include",
    ]
    if not os.path.exists(rt_obj):
        run(["clang", "--target=i386-none-elf", "-m32", "-std=gnu99", "-ffreestanding",
             "-mno-sse", "-mno-sse2", "-mno-mmx", "-fno-pic", "-fno-stack-protector",
             "-fno-builtin", "-Os", f"-I{ROOT}/include", f"-I{ROOT}/sdk",
             "-c", f"{ROOT}/userland/rt.c", "-o", rt_obj])

    run(["clang"] + cflags + ["-c", os.path.join(ROOT, src_rel), "-o", obj])
    run(["ld.lld", "-m", "elf_i386", "-nostdlib", "-T", f"{ROOT}/userland/app.ld", "-o", elf, obj, rt_obj])
    run(["llvm-objcopy", "-O", "binary", elf, bin_p])
    run([sys.executable, f"{ROOT}/tools/airpack.py", bin_p, airp, name])
    run([sys.executable, f"{ROOT}/tools/pancake.py", "pack", pck, airp] + [os.path.join(ROOT, f) for f in extra_files])
    run([sys.executable, f"{ROOT}/tools/pancake.py", "verify", pck])
    print(f"[OK] {name}: {airp} + {pck}")

if __name__ == "__main__":
    build_sdk_airp("toybox", "ported/developer/toybox/toybox_mini.c", ["ported/developer/toybox/toybox_mini.h"])
    build_sdk_airp("kilo", "ported/developer/kilo/kilo_core.c", ["ported/developer/kilo/kilo_core.h"])
