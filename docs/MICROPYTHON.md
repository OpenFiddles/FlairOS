# MicroPython on FlareOS

FlareOS has an experimental native i386 port for upstream MicroPython v1.29.0.

The standard `make` image does not include the interpreter. The optional boot
slot reserves 257 sectors (128 KiB) for a MicroPython AIRP package, and Stage 1
probes its magic before loading it. The current interpreter package is larger
than that slot, so `make python-image` intentionally stops at its size check
instead of creating a truncated or unbootable image. The package must be reduced
or the boot/storage path expanded before a Python-enabled image can be produced.

Choose **Python** from the desktop Start menu, or type `python` in the text
shell. From the AirGraph pixel desktop, the Start menu temporarily switches to
the VGA text console for the REPL and restores the pixel desktop after
`flareos.exit()`. The interpreter uses a 384 KiB garbage-collected heap allocated through
`SYS_BRK`, with a 12 KiB stack-check limit. In the GUI Terminal, use Ctrl+Up and Ctrl+Down to page through recent output; the right-edge scrollbar indicates the current position. It exposes the FlareOS display,
keyboard, pointer, timer, speaker, and filesystem APIs through `import flareos`.
Use `flareos.exit()` to leave the REPL and return to the desktop.

The browser's **Load local boot image** control accepts a bootable raw floppy
image or ISO, passed directly to v86 without uploading it. It can be used with a
Python-enabled image once the interpreter package fits the boot format.

This is a minimal MicroPython port, not CPython. VFS imports, POSIX, sockets,
threads, and CPython extension modules are not enabled. The interpreter package
is currently documented at approximately 584 KiB, while the AIRP boot slot is
128 KiB. This is a packaging limitation, not a reason to change the default OS
build. Runtime testing is still required before treating the port as
production-ready.
