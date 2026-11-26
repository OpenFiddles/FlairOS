#!/usr/bin/env python3
"""FlairOS Preset Folder Build System (x86_64 & aarch64)."""
import argparse, json, os, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

COMMON_SRCS = [
    "kernel/mm/kmem64.c",
    "kernel/boot/kmain64.c",
    "kernel/gfx/font5x7.c",
    "kernel/gfx/bmp.c",
    "kernel/fs/vfs.c",
    "kernel/pkg/apk.c",
    "kernel/pkg/dex.c",
    "kernel/mm/binder.c",
    "kernel/mm/ashmem.c",
    "graphics/compositor/glass_fx.c",
    "runtime/art/art_bridge.c",
    "runtime/art/sandbox.c",
    "net/airp/airp_net.c",
    "drivers/bt/bt_hci.c",
    "net/bt/bt_core.c",
    "ported/developer/libs/flair_posix/flair_posix.c",
    "ported/developer/libs/flair_zlib/flair_zlib.c",
    "ported/developer/openjdk/src/jvm_class.c",
    "ported/developer/toybox/toybox_mini.c",
    "ported/developer/kilo/kilo_core.c",
    "ported/python/flair_py.c",
    "ui/shells/flair_browser.c",
    "ui/systemui/airstin_ui.c",
    "net/service/net_service.c",
    "runtime/art/android_libs.c",
    "runtime/art/dex_interp.c",
    "runtime/art/surface_flinger.c",
    "kernel/core/syscall64.c",
    "kernel/pkg/elf64.c",
    "net/bridge/host_bridge.c",
]

def run(cmd):
    res = subprocess.run(cmd, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if res.returncode != 0:
        print(res.stdout)
        raise RuntimeError(f"Command failed ({res.returncode}): {' '.join(cmd)}")
    return res.stdout

def build_one(preset_name, arch):
    pjson = os.path.join(ROOT, "presets", preset_name, "preset.json")
    if not os.path.exists(pjson):
        raise RuntimeError(f"Unknown preset: {preset_name}")
    cfg = json.load(open(pjson))
    if arch not in cfg["supported_arches"]:
        raise RuntimeError(f"Preset {preset_name} does not support arch {arch}")

    out_dir = os.path.join(ROOT, "build-presets", f"{preset_name}-{arch}")
    os.makedirs(out_dir, exist_ok=True)

    target = "x86_64-none-elf" if arch == "x86_64" else "aarch64-none-elf"
    cflags = [
        f"--target={target}",
        "-std=gnu99", "-ffreestanding", "-fno-pic", "-fno-stack-protector",
        "-fno-builtin", "-fno-common", "-Os", "-Wall", "-Wextra",
        f"-I{os.path.join(ROOT, 'include')}",
        f"-DFLAIROS_64BIT",
        f"-DPRESET_NAME=\"{preset_name}\"",
        f"-DPRESET_W={cfg['width']}",
        f"-DPRESET_H={cfg['height']}",
        f"-DPRESET_AIRP_ROLE={cfg['airp_role']}",
        f"-DFB_INIT_W={cfg['width']}",
        f"-DFB_INIT_H={cfg['height']}",
    ] + [f"-D{d}" for d in cfg.get("defines", [])]

    if arch == "x86_64":
        cflags += ["-mno-red-zone", "-mcmodel=small"]
        arch_srcs = ["arch/x86_64/boot.S", "arch/x86_64/hal.c", "arch/x86_64/bochs_fb.c", "arch/x86_64/input.c"]
        ld_script = os.path.join(ROOT, "arch/x86_64/linker.ld")
        ld_emul = "elf_x86_64"
    else:
        cflags += ["-mstrict-align"]
        arch_srcs = ["arch/aarch64/boot.S", "arch/aarch64/hal.c", "arch/aarch64/ramfb.c", "arch/aarch64/input.c"]
        ld_script = os.path.join(ROOT, "arch/aarch64/linker.ld")
        ld_emul = "aarch64elf"

    srcs = arch_srcs + COMMON_SRCS + [cfg["shell_source"]]
    objs = []
    for src in srcs:
        obj = os.path.join(out_dir, os.path.basename(src) + ".o")
        cmd = ["clang"] + cflags + ["-c", os.path.join(ROOT, src), "-o", obj]
        out = run(cmd)
        if out.strip():
            print(out.strip())
        objs.append(obj)

    elf = os.path.join(out_dir, f"flairos-{preset_name}-{arch}.elf")
    run(["ld.lld", "-m", ld_emul, "-nostdlib", "-T", ld_script, "-o", elf] + objs)

    if arch == "x86_64":
        mb_elf = os.path.join(out_dir, f"flairos-{preset_name}-{arch}.mb.elf")
        run([sys.executable, os.path.join(ROOT, "arch/x86_64/mb_wrap.py"), elf, mb_elf])
        print(f"[OK] {preset_name}/{arch}: {mb_elf} ({os.path.getsize(mb_elf)} bytes)")
    else:
        bin_path = os.path.join(out_dir, f"flairos-{preset_name}-{arch}.bin")
        run(["llvm-objcopy", "-O", "binary", elf, bin_path])
        print(f"[OK] {preset_name}/{arch}: {elf} ({os.path.getsize(elf)} bytes)")

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--preset", default="desktop")
    ap.add_argument("--arch", default="x86_64")
    ap.add_argument("--all", action="store_true")
    args = ap.parse_args()
    if args.all:
        run([sys.executable, os.path.join(ROOT, 'tools', 'build_apps.py')])
        for p in ("desktop", "mobile", "tv"):
            for a in ("x86_64", "aarch64"):
                build_one(p, a)
    else:
        build_one(args.preset, args.arch)
