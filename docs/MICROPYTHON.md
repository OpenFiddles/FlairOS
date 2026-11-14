# MicroPython on FlareOS

FlareOS has an experimental i386 port layer for upstream MicroPython v1.29.0.
See [the port README](../ports/micropython/README.md) for its API and current
limitations.

Build it separately with `make micropython-port`. Normal kernel and disk
image targets do not fetch, compile, or install MicroPython.
