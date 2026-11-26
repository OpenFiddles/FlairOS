# Memory architecture

Memory on i386 is a fixed map plus two allocators. The map is the single most
important constraint in the system, because it is why the kernel has a hard
static-size ceiling.

## Physical map

| Range | Use | Owner |
| --- | --- | --- |
| `0x00000`–`0x004FF` | Real-mode IVT, BIOS data | Firmware |
| `0x00500`–`0x07BFF` | Conventional RAM | General |
| `0x07C00`–`0x07DFF` | Boot sector | Stage 1 |
| `0x09000`–`0x09FFF` | Stage 2 | `stage2.asm` |
| `0x05000` | E820 region count | `drivers/pmm.c` |
| `0x05010` | E820 entries | `drivers/pmm.c` |
| `0x10000`–`0x2FFFF` | **Kernel image** | `kernel/boot/link.ld` |
| `0x30000`–`0x33FFF` | Settings package staging | `stage1.asm` |
| `0x34000`–`0x37FFF` | Notes package staging | `stage1.asm` |
| `0x38000`–`0x3FFFF` | Terminal package staging | `stage1.asm` |
| `0x40000`–`0x7FFFF` | **Application slot** | `airp_load` |
| `0x60000`–`0x9FFFF` | MicroPython staging (optional) | `stage1.asm` |
| `0x80000`–`0x85FFF` | PMM bitmaps | `drivers/pmm.c` |
| `0x90000`–`0x93FFF` | Explorer package staging | `stage1.asm` |
| `0x94000`–`0x97FFF` | Desktop package staging | `stage1.asm` |
| `0xA0000`–`0xBFFFF` | VGA aperture (framebuffer) | VBE |
| `0xB8000`–`0xB8F9F` | VGA text buffer | `kernel/core/kernel.c` |
| `0x100000`+ | Managed frames | PMM |

The MicroPython and package staging regions overlap intentionally: MicroPython is
optional and only loads when its magic is present, and the desktop packages it
could collide with are loaded before it.

## The 0x30000 ceiling

`kernel/boot/link.ld` asserts:

```
ASSERT(__bss_end <= 0x30000, "kernel bss overlaps the reserved PMM bitmap and Explorer staging gap")
```

This is not arbitrary. `0x30000` is where the Settings package is staged at boot,
so kernel `.bss` growing past it would be overwritten by a package. The
assertion is deliberately hard: the previous work on this project resolved two
separate overflow incidents by **reducing static footprint**, not by relaxing the
limit (see `agent/ChatGPT/decisions.md`).

Current state: `__bss_end` is `0x2E8C8`, leaving about 5.9 KB.

That headroom is why the desktop theme loader (`kernel/pkg/airjson.c` plus
`kernel/ui/desktop_theme.c`, about 6.9 KB of text) is **not linked into the i386
image yet**. It is complete and host-tested, but linking it would exceed the
ceiling by roughly 1 KB. The options are to reclaim static memory elsewhere or
to move staged packages out of the `0x30000` region; both are roadmap items, and
neither should be done by weakening the assertion.

## Physical frame allocator

`drivers/pmm.c` reads the E820 map into a frame allocator in `drivers/mm_core.c`.
Frames are 4 KiB, managed from `0x100000` to `0x10000000` (256 MB). Three bitmaps
track availability, management and reservation.

Bitmaps are static at `0x80000`, `0x82000` and `0x84000`, sized for the maximum
managed range. `pmm_selftest()` allocates, frees and double-frees frames during
boot.

## Virtual memory

`drivers/vm.c` builds a page directory that identity-maps the first 4 GB using
4 MiB pages, then splits entries to 4 KiB pages when a region needs finer
control. Kernel virtual allocation replaces identity PTEs; application address
spaces get a private directory with copied frames.

Identity mapping keeps physical and virtual addresses equal for everything below
4 GiB, which is why the boot staging addresses above are also valid kernel
pointers.

## Heap

`drivers/kheap.c` provides `kmalloc`/`kfree` over a kernel-only arena. A
self-test runs during boot. The boot app snapshot is the largest single early
allocation (`128 * 512 = 64 KB`).

## Application memory

An application receives:

| Region | Address | Notes |
| --- | --- | --- |
| Image | `0x40000` | Fixed, copied from the package payload |
| Stack | top at `0x7F000` | Grows down |
| Heap | separate page table | `brk`-style growth via `SYS_MMAP` |

`AIRP_SLOT_SIZE` is 256 KiB, so `airp_load` clears the slot after the image to
ensure `.bss` starts zeroed for every launch.

## Shared memory

`kernel/mm/ashmem.c` provides named shared regions and `kernel/mm/binder.c` an
IPC-style handle registry, both used by the application runtime research path
rather than by the core desktop.
