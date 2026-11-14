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
print(flareos.pointer())          # (x, y, button_bits)
print(flareos.get_arrow())        # arrow code or None
print(flareos.ticks_ms())         # PIT ticks, 10 ms resolution
flareos.sleep_ms(100)
flareos.beep(523, 120)
print(flareos.fs_list())
flareos.fs_write("HELLO.TXT", "Hello from Python")
print(flareos.fs_read("HELLO.TXT"))
flareos.fs_rename("HELLO.TXT", "HELLO.BAK")
print(flareos.fs_slots())

handle = flareos.fs_open("HELLO.BAK", flareos.FS_READ)
data = flareos.fs_read_handle(handle, 512)
flareos.fs_seek(handle, 0)
flareos.fs_close(handle)
flareos.run("SCRIPT.PY")
```

The module bridges console output/clear, keyboard input and arrow keys, pointer
state, PIT time/sleep, the PC speaker, and filesystem operations. The filesystem
supports 16 files up to 64 KiB each and 8 open handles. It commits files to ATA
storage when an ATA disk is detected; without one, it operates as a RAM fallback
and those files are lost on reboot. It exposes both whole-file helpers and low-level handle operations (open,
read, write, seek, close), plus listing, existence, rename, remove, and the
free-slot count. Filenames are 1–12
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
