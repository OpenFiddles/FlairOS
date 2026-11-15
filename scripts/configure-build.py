#!/usr/bin/env python3
"""Interactive FlairOS build-target setup wizard."""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

ARCHES = [
    ("i686", "i686 / v86 emulator (current x86 build)"),
    ("aarch64", "AArch64 / ARM64 (native ARM bring-up)"),
]
DEVICES = [
    ("mobile", "Mobile phone", "Portrait-first phone UI; touch input"),
    ("tablet", "Tablet", "Adaptive tablet UI; portrait and landscape"),
    ("desktop", "Desktop / laptop", "Windowed desktop UI; landscape-first"),
    ("smartwatch", "Smartwatch", "Compact, glanceable, usually square UI"),
    ("tv", "Television", "10-foot UI; landscape and remote-control focus"),
    ("embedded", "Embedded / kiosk", "Fixed-purpose UI; configurable display"),
]
ORIENTATIONS = [
    ("auto", "Automatic (recommended)"),
    ("portrait", "Portrait"),
    ("landscape", "Landscape"),
    ("square", "Square"),
]


def choose(title: str, entries: list[tuple[str, str]], default: int) -> str:
    print(f"\n{title}")
    for index, (_, label) in enumerate(entries, 1):
        marker = " [default]" if index - 1 == default else ""
        print(f"  {index}. {label}{marker}")
    while True:
        raw = input(f"Select [1-{len(entries)}] (Enter = {default + 1}): ").strip()
        if not raw:
            return entries[default][0]
        if raw.isdigit() and 1 <= int(raw) <= len(entries):
            return entries[int(raw) - 1][0]
        print("Please enter one of the listed numbers.")


def main() -> int:
    parser = argparse.ArgumentParser(description="Configure a FlairOS device build.")
    parser.add_argument("--no-build", action="store_true", help="Configure only; do not compile.")
    args = parser.parse_args()

    print("=" * 54)
    print("                 FlairOS Build Setup")
    print("=" * 54)
    print("Choose the CPU architecture and the device experience.")
    print("These are separate choices: the same device profile can")
    print("eventually run on more than one CPU architecture.")

    arch = choose("1/3  CPU architecture", ARCHES, 1)
    device = choose("2/3  What device is this build for?", DEVICES, 0 if arch == "aarch64" else 2)
    orientation_default = {"mobile": 1, "tablet": 0, "desktop": 2,
                           "smartwatch": 3, "tv": 2, "embedded": 0}[device]
    orientation = choose("3/3  Preferred display orientation", ORIENTATIONS, orientation_default)

    build_dir = ROOT / f"build-{arch}-{device}"
    toolchain = ROOT / ("cmake/toolchains/aarch64-linux.cmake"
                        if arch == "aarch64" else "cmake/toolchain-i686-clang.cmake")
    if not toolchain.is_file():
        print(f"Missing toolchain file: {toolchain}", file=sys.stderr)
        return 2

    print("\nBuild configuration")
    print(f"  Architecture : {arch}")
    print(f"  Device       : {device}")
    print(f"  Orientation  : {orientation}")
    print(f"  Build folder : {build_dir.relative_to(ROOT)}")
    if device == "smartwatch":
        print("  Note         : smartwatch profile only; this does not create an Apple watchOS app.")
    if arch == "i686" and device in {"mobile", "tablet", "smartwatch", "tv"}:
        print("  Note         : profile metadata is selected, but this architecture currently")
        print("                 builds the existing i686/v86 kernel, not native device hardware.")

    if input("\nConfigure this build? [Y/n]: ").strip().lower() in {"n", "no"}:
        print("Cancelled; no build files were changed.")
        return 0

    command = [
        "cmake", "-S", str(ROOT), "-B", str(build_dir), "-G", "Ninja",
        f"-DCMAKE_TOOLCHAIN_FILE={toolchain}",
        f"-DFLAIROS_ARCH={arch}",
        f"-DFLAIROS_DEVICE={device}",
        f"-DFLAIROS_ORIENTATION={orientation}",
    ]
    subprocess.run(command, check=True)

    if args.no_build:
        print("\nConfiguration generated. Compile later with:")
        print(f"  cmake --build {build_dir.relative_to(ROOT)}")
        return 0

    print("\nStarting compilation...")
    subprocess.run(["cmake", "--build", str(build_dir), "--parallel"], check=True)
    print("\nBuild completed.")
    if arch == "aarch64":
        print(f"Kernel: {build_dir.relative_to(ROOT)}/aarch64/kernel.elf")
        print(f"Run:    cmake --build {build_dir.relative_to(ROOT)} --target aarch64_run")
    else:
        print(f"Image:  {build_dir.relative_to(ROOT)}/textile.img")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except subprocess.CalledProcessError as exc:
        print(f"Build command failed with exit code {exc.returncode}.", file=sys.stderr)
        raise SystemExit(exc.returncode)
    except KeyboardInterrupt:
        print("\nSetup cancelled.", file=sys.stderr)
        raise SystemExit(130)
