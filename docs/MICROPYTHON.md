# MicroPython on FlareOS

FlareOS has an experimental i386 port layer for upstream MicroPython v1.29.0.
See [the port README](../ports/micropython/README.md) for its API and current
limitations, including the ATA-backed filesystem and RAM-only fallback when no
ATA disk is detected.

Build it separately with `make micropython-port`. Normal kernel and disk
image targets do not fetch, compile, or install MicroPython.
