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
