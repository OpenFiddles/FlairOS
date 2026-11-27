# Building FlareOS

A small x86 hobby kernel: a 512-byte boot sector loads a 64-sector kernel, switches to 32-bit protected mode, and runs a shell.

- Hardware it talks to: VGA text (0xB8000 and cursor ports), PS/2 keyboard (IRQ1), PIT timer (IRQ0), PIC, CMOS clock (ports 0x70/0x71), RTL8139 network card (PCI config and I/O ports, detection only for now), COM1 serial (mirrors the screen for tests), and any I/O port through `in` and `out`.
- Shell commands: `help`, `clear`, `echo`, `time`, `uptime`, `in PORT`, `out PORT VALUE`, `reboot`, `net`, `sqr X Y`.
- Boot shows a short Saturn splash with a pulsing background before the shell starts.

## Build and run

The product is the 64-bit line: desktop, mobile and TV shells on x86_64 and
aarch64. It needs clang, ld.lld, llvm-objcopy and python3.

    make                      # builds all six 64-bit preset images
    make verify-disk          # boots each x86_64 disk image, checks the screen
    ./flairos-start.sh verify # boots the -kernel images headless
    make test                 # host tests over the shared kernel sources

The i386 image is the legacy reference build. It is kept bootable but is no
longer built by default; it needs nasm, gcc with 32-bit support (gcc-multilib on
Debian/Ubuntu), ld, objcopy, xorriso and qemu-system-i386.

    make legacy      # builds build/textile.img, the LiveCD and the Setup boot CD
    make legacy-test # host tests plus the i386 boot, layout and media tests
    make run         # boots the i386 image in QEMU, serial output in the terminal

## Building on the phone, in Termux

The aarch64 target builds on the device itself: Termux's clang emits aarch64
freestanding code, so no cross toolchain is involved.

    pkg install cmake ninja clang binutils
    tools/build-termux.sh              # -> build-arm64/flairos-arm64.bin
    LEGACY=1 tools/build-termux.sh     # also build-termux-i386/textile.img

The script is the same CMake + Ninja build CI uses (`cmake --preset
arm64-phone`); it only adds the dependency checks. Verified here on an x86_64
Linux host with clang 18; the Termux run itself has not been done on a phone.

## APK support: what is real today

`make apk-probe APK=path/to/app.apk` runs a real package through the same code
the kernel runs: the ZIP walk in `kernel/pkg/apk.c`, the raw-DEFLATE decoder in
`ported/developer/libs/flair_zlib`, and the DEX reader in `kernel/pkg/dex.c`.

What it really does today, measured on two real packages:

- 12.9 MB F-Droid client: 1227 ZIP entries, `classes.dex` + `classes2.dex` both
  deflated, both extracted and inflated to 10,566,820 and 10,330,144 bytes, both
  validated as `dex\n035`, 9138 and 9491 class definitions.
- 124 MB Firefox for Android (Fennec 160.0.1): 3995 entries, three dex files
  inflated (13.5 MB, 8.7 MB, 9.9 MB), all `dex\n038`, 15,363 / 11,424 / 14,513
  class definitions.

What it does not do: run them. `dex_run_buffer` returns -4 for every real dex
because it starts at the first `class_def` and that class carries no `code_off`;
it is also not a Dalvik VM (no objects, strings, exceptions, JNI, JIT or native
`.so` loading). Chrome and Firefox additionally depend on their own native
browsers and WebView. So: packages are readable, not executable. The probe
prints exactly which stage a package reaches instead of implying more.
