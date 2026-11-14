# MicroPython on FlareOS

FlareOS has an experimental native i386 port for upstream MicroPython v1.29.0.

Build a separate Python-enabled boot image with `make python-image`. The
standard `make` image remains unchanged and does not include the interpreter.
The optional package is stored in the unused final 257 sectors of the 1.44 MiB
disk image. Stage 1 probes its AIRP magic and only reads the remaining sectors
when the package is present.

Choose **Python** from the desktop Start menu, or type `python` in the text
shell. The interpreter uses a 384 KiB garbage-collected heap allocated through
`SYS_BRK`, with a 12 KiB stack-check limit. It exposes the FlareOS display,
keyboard, pointer, timer, speaker, and filesystem APIs through `import flareos`.
Use `flareos.exit()` to leave the REPL and return to the desktop.

This is a minimal MicroPython port, not CPython. VFS imports, POSIX, sockets,
threads, and CPython extension modules are not enabled. The separate CI job
builds and packages the interpreter and publishes the Python-enabled disk image
as an artifact. Runtime testing is still required before treating the port as
production-ready.
