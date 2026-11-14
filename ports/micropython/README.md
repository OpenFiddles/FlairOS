# FlareOS MicroPython port (experimental)

This is a native i386 port layer for **MicroPython v1.29.0**. It targets the
existing FlareOS ring-3 flat-app ABI and uses the upstream MicroPython core;
it is not a Python reimplementation.

## Build

From the repository root, run `make micropython-port`. The target fetches the
pinned upstream tag into `build/third_party/micropython`, copies this port
into that checkout, and builds `build/micropython/flareos.elf` and
`build/micropython/flareos.bin`. The upstream source tree is not committed.

## Python API

```python
import flareos
print(flareos.screen())           # (80, 25)
print(flareos.ticks_ms())         # PIT ticks, 10 ms resolution
flareos.sleep_ms(100)
flareos.beep(523, 120)
print(flareos.fs_list())
flareos.fs_write("HELLO.TXT", "Hello from Python")
print(flareos.fs_read("HELLO.TXT"))
flareos.run("SCRIPT.PY")
```

The module bridges console output/clear, keyboard input, PIT time/sleep,
the PC speaker, and persistent flat-file operations. Filenames are 1–12
characters with no directory separator. Files are limited to 64 KiB.
`flareos.run(name)` executes Python source stored in the filesystem.

## Runtime boundary

The interpreter uses MicroPython's garbage collector with a 384 KiB arena
allocated from the FlareOS app heap through `SYS_BRK`; stack checks are capped
at 12 KiB. VFS imports, POSIX, sockets, threads, and CPython extension modules
are not enabled in this minimal profile.

The current `.airp` application container is limited to 64 KiB. The build
reports the actual binary size, and `make micropython-package` checks that
limit before creating an `.airp`. It does not claim a larger image can be
loaded. Boot-image integration remains separate until size and runtime tests
succeed.
