# Kernel

The kernel tree is grouped by subsystem. `kernel.h` and `textile.h` stay at the
root because every layer includes them; the build adds `kernel/` to the include
path so `#include "kernel.h"` works from any subdirectory.

```
kernel/
├── kernel.h, textile.h   shared internal headers
├── boot/                 entry points, boot protocol, firmware discovery
│   ├── entry.asm         i386 entry: clear BSS, call kmain (and image header)
│   ├── isr.asm           interrupt stubs, syscall gate, context switch
│   ├── usermode.asm      ring-3 entry
│   ├── link.ld           i386 link script with the 0x30000 static ceiling
│   ├── boot.c            parses the loader's boot-info block
│   ├── hwinfo.c          CPU, PCI, memory, ACPI and SMBIOS inventory
│   ├── setup.c           setup-media flow
│   └── kmain64.c         portable 64-bit entry (x86_64 and AArch64 presets)
├── core/                 scheduler, syscalls, kernel main, panics
├── mm/                   physical and virtual memory helpers, IPC heaps
├── fs/                   VFS, FAT32, persistent filesystem
├── pkg/                  AIRP containers, APK, DEX, ELF loaders, JSON
├── gfx/                  AirGraph, framebuffer, VBE, BMP, themes
├── ui/                   desktop and mobile shells, WinDown, chrome
└── dev/                  keyboard, mouse, PIT, network driver
```

Where the boundaries are:

- `gfx/` draws pixels; `ui/` arranges windows and widgets.
- `pkg/` understands container formats; `fs/` understands storage.
- `boot/` is the only place that talks to firmware.
