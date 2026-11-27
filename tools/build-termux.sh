#!/bin/sh
# Build FlairOS from Termux on an Android phone.
#
# The phone is an aarch64 device, so the default build is the arm64-phone
# preset: CMake + Ninja with Termux's clang and lld, producing a freestanding
# aarch64 image. Nothing here needs root, a desktop machine, or a cross
# toolchain: Termux clang emits aarch64 directly.
#
#   pkg install cmake ninja clang binutils   # once
#   tools/build-termux.sh                    # builds build-arm64/flairos-arm64.bin
#   LEGACY=1 tools/build-termux.sh           # also the i386 image, if you want it
#
# The i386 path is the legacy reference build. It needs nasm and clang with the
# i386 backend (Termux clang has it); it is opt-in here for the same reason it is
# opt-in everywhere else.
set -eu

say() { printf '%s\n' "$*"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

need() {
    command -v "$1" >/dev/null 2>&1 || die "missing $1 (pkg install $2)"
}

cd "$(dirname "$0")/.."

say "host: $(uname -m) $(uname -s), Termux=${TERMUX_VERSION:-no}"
need cmake cmake
need ninja ninja
need clang clang
need python3 python
say "cmake: $(cmake --version | head -1)"
say "ninja: $(ninja --version)"
say "clang: $(clang --version | head -1)"

say ""
say "== arm64-phone (the product target) =="
cmake --preset arm64-phone
cmake --build --preset arm64-phone
test -s build-arm64/flairos-arm64.bin || die "build-arm64/flairos-arm64.bin missing"
say "built build-arm64/flairos-arm64.bin ($(wc -c < build-arm64/flairos-arm64.bin) bytes)"

if [ "${LEGACY:-0}" = "1" ]; then
    say ""
    say "== i386 legacy reference image (opt-in) =="
    need nasm nasm
    command -v ld.lld >/dev/null 2>&1 || die "missing ld.lld (pkg install binutils)"
    cmake -S . -B build-termux-i386 -G Ninja \
          -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-i686-clang.cmake
    ninja -C build-termux-i386
    test -s build-termux-i386/textile.img || die "build-termux-i386/textile.img missing"
    say "built build-termux-i386/textile.img ($(wc -c < build-termux-i386/textile.img) bytes)"
fi

say ""
say "done. Copy the image to a phone/emulator and boot it; nothing was flashed."
