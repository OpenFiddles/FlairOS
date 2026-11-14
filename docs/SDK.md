# FlareOS app SDK (v1.3)

Apps are flat x86 binaries. They run in ring 3 and call the kernel with `int 0x80`.

| Header | What it gives you | Needs the kernel? |
| --- | --- | --- |
| `sdk/flareos.h` | Kernel calls: screen, keys, timer, tones, files | Yes |
| `sdk/flare_string.h` | Memory and string helpers, number text, a bump heap | No (host-testable) |
| `sdk/flare_libc.h` | File streams and number printing, built on the two above | Yes |

## Make an app

1. Include `"flare_libc.h"` (or `"flareos.h"` alone for kernel calls only).
2. Write `void app_main(void)`.
3. Put `FLARE_APP();` once at file level. It places the header the kernel reads.

`examples/hello` and `examples/libc_demo` are working examples. `make sdk-example` builds both to `build/*.bin` and fails if either is over 65536 bytes.

## Kernel calls (`flareos.h`)

| Function | What it does |
| --- | --- |
| `flare_print(s)` | Print a string to the text screen |
| `flare_putc(ch)` | Print one character at the cursor |
| `flare_put_cell(row, col, ch, attr)` | Draw one cell (80x25 VGA text) |
| `flare_clear()` | Clear the screen |
| `flare_ticks()` | Timer ticks since boot (about 100 per second) |
| `flare_sleep(ticks)` | Wait for ticks |
| `flare_tone(hz, ms)` | Play a tone (20 to 20000 Hz; 0 turns the speaker off) |
| `flare_anim_start(slot, ease, ticks, from, to)` | Start a tween in one of 16 slots |
| `flare_anim_value(slot)` | Read a tween's current value |
| `flare_getkey()` | Next keyboard character, or -1 if none |
| `flare_get_arrow()` | Arrow code, or -1. The kernel has no handler for it yet, so it returns -1 today |
| `flare_pointer()` | Pointer x, y (text cells) and buttons |
| `flare_screen(&cols, &rows)` | Text grid size (80 x 25 today) |
| `flare_exit()` | Return to the desktop (called automatically when `app_main` returns) |

### Files (kernel)

| Function | What it does |
| --- | --- |
| `flare_fs_open(name, mode)` | Open with `FLARE_FS_READ`, `FLARE_FS_WRITE` (truncates) or `FLARE_FS_APPEND`. Returns a handle (1 to 8) or `FLARE_FS_FAIL` |
| `flare_fs_read(h, buf, max)` | Read up to `max` bytes; returns the count read |
| `flare_fs_write(h, buf, count)` | Write; returns the count written |
| `flare_fs_seek(h, pos)` | Move to `pos` (0 to size) |
| `flare_fs_size(h)` | Size of the open file |
| `flare_fs_close(h)` | Release the handle |
| `flare_fs_delete(name)` | Delete a file that has no open handle |
| `flare_fs_rename(old, new)` | Rename; fails if `new` exists or `old` is open |
| `flare_fs_exists(name)` | 1 if the file exists, else 0 |
| `flare_fs_list(index, name_out)` | Size of the file at `index`; `name_out` needs 13 bytes |
| `flare_fs_slots()` | Unused file slots out of 16 |

## Strings and memory (`flare_string.h`)

| Function | What it does |
| --- | --- |
| `flare_memset(dst, v, n)`, `flare_memcpy(dst, src, n)` | Byte copy and fill |
| `flare_memmove(dst, src, n)` | Copy that is safe when the ranges overlap |
| `flare_memcmp(a, b, n)` | Negative, 0 or positive |
| `flare_strlen(s)`, `flare_strcmp(a, b)` | Length and comparison |
| `flare_strlcpy(dst, src, n)` | Copies at most n-1 bytes and always terminates (n > 0). Returns strlen(src) |
| `flare_utoa(v, out)`, `flare_itoa(v, out)` | Decimal text. `out` needs 11 bytes (utoa) or 12 (itoa) |

## Heap (`flare_string.h`)

| Function | What it does |
| --- | --- |
| `flare_heap_init(&h, buf, size)` | Use `buf` as the arena. It must be 8-byte aligned |
| `flare_heap_alloc(&h, n)` | Returns an 8-byte-aligned block, or NULL when it does not fit |
| `flare_heap_zalloc(&h, n)` | Same, zeroed |
| `flare_heap_reset(&h)` | Frees every block from this heap at once |
| `flare_heap_free_bytes(&h)` | Bytes left |

This is a bump allocator. There is no per-block free. It is not the kernel's memory manager.

## Streams and printing (`flare_libc.h`)

| Function | What it does |
| --- | --- |
| `flare_fopen(&f, name, mode)` | Mode `'r'`, `'w'` or `'a'`. Returns 0, or -1 on failure |
| `flare_fread(&f, buf, max)` | Bytes read; 0 at end of file or on failure |
| `flare_fwrite(&f, buf, count)` | Bytes written; 0 on failure |
| `flare_fputs(&f, s)` | Writes the whole string; 0 on success, -1 otherwise |
| `flare_fsize(&f)`, `flare_fseek(&f, pos)` | Size and seek |
| `flare_fclose(&f)` | Releases the handle |
| `flare_print_uint(v)`, `flare_print_int(v)` | Prints a number |

## Storage

Files are held in kernel memory and, when a supported ATA disk with a valid FlareOS directory is present, written through to disk. The directory is at LBA 398; file slot N owns a fixed 128-sector (64 KiB) region beginning at LBA 399 + N × 128. The filesystem has 16 slots, supports files up to 65536 bytes, and exposes 8 simultaneous handles. `format` initializes the directory and is destructive to any existing FlareOS filesystem metadata at LBA 398; do not run it on a disk whose contents you want to preserve.

## Limits

- 65536 bytes per app. The image is a flat binary, so zero-initialized static data (`.bss`) is not stored in it. Do not rely on a static buffer starting zeroed; use `flare_heap_zalloc` or write before you read.
- Names are up to 12 characters. A file holds up to 65536 bytes. There are 16 files and 8 open handles, with one open handle per file.
- The SDK's example heap is bump-only. The experimental MicroPython port uses its own garbage-collected arena from the kernel's `SYS_BRK` heap instead.
- The window API (`include/axi.h`) is available to kernel/userland code but is not yet wrapped by the small C SDK.
- Strings passed to kernel calls are read by the kernel. Passing a stack or heap buffer has not been tested on physical hardware.
- MicroPython: an experimental i386 port exists under `ports/micropython`. It is built separately with `make micropython-port`; it is not yet installed into the boot image or launchable from the desktop. It still needs a successful upstream build and a compatible application-image size/loader path. This is MicroPython, not CPython.

## Checks

- `make sdk-test` runs the host checks for `flare_string.h`.
- `make sdk-example` builds the two examples and checks their size.
- `make fs-test` runs the filesystem checks on the host.
- `tests/smoke.py` boots the image in QEMU and checks the desktop, the shell, the app loader and the disk format.
