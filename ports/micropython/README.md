# FlareOS MicroPython port (experimental)

This is a native i386 port layer for **MicroPython v1.29.0**. It targets the
existing FlareOS ring-3 flat-app ABI and uses the upstream MicroPython core;
it is not a Python reimplementation.

## Build

From the repository root, run `make python-image`. This builds a separate
`build/textile-python.img` containing the optional MicroPython package in the
reserved tail of the floppy image. The ordinary `make` image remains lightweight. The target fetches the
pinned upstream tag into `build/third_party/micropython`, copies this port
into that checkout, and builds `build/micropython/flareos.elf` and
`build/micropython/flareos.bin`. The upstream source tree is not committed.

## Python API

```python
import flareos
print(flareos.screen())           # (80, 25)
# Leave the REPL and return to the desktop:
# flareos.exit()
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

The `.airp` image limit is 128 KiB, within a 256 KiB private app/stack slot.
The optional Python package is stored in the final 257 sectors of the 1.44 MiB
boot image and loaded only when its AIRP magic is present. Select **Python** in
the desktop Start menu or type `python` in the text shell. Use `flareos.exit()`
to return to the desktop. The standard disk image does not include Python;
`make python-image` builds the Python-enabled variant.
