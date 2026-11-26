# Filesystem architecture

FlairOS has a VFS layer with two backing modes: a persistent FAT32 volume when an
ATA disk is present, and a RAM-backed volume when it is not.

## Layers

| Layer | File | Responsibility |
| --- | --- | --- |
| VFS | `kernel/fs/vfs.c` | Path resolution, handles, directory listing |
| FAT32 | `kernel/fs/fat32.c` | On-disk FAT32 read/write |
| Block device | `drivers/drv_ata.c` | ATA PIO sector access |
| RAM seed | `kernel/fs/vfs.c` | Built-in files when no disk exists |

## Path model

Paths are absolute from a single root. VFS provides `open`, `read`, `write`,
`close`, `mkdir`, `readdir`, `stat`, `chdir`, `getcwd` and `unlink`, mirrored as
syscalls 47–56 so applications use the same interface the kernel does.

The design is deliberately Unix-shaped: one root, `/`-separated components, a
current working directory per session rather than per process.

## Storage modes

`vfs_has_persistent_storage()` decides the mode at boot:

| Mode | Condition | Behaviour |
| --- | --- | --- |
| Persistent | ATA disk identified and FAT32 mounted | Files survive reboot |
| RAM | No ATA disk | Files exist for the session only |

RAM mode is a real, supported mode, not a degraded failure. The boot log says so
explicitly (`storage: no persistent disk; running in RAM mode`), and the kernel
refuses to continue if there is no persistent disk **and** less than 16 MB free,
because a session with neither is not viable.

`format` erases the volume. It is a destructive operation exposed only in the
developer console, never in the desktop UI.

## Kernel-private files

Some files are read by kernel subsystems rather than by applications:

| File | Consumer | Content |
| --- | --- | --- |
| `UI.CFG` | `desktop_gfx_init` | Profile, touch mode, animation flags |
| `SYS.LOG` | Developer console | Debug log dump on `Ctrl+Alt+D` |
| `NOTE.TXT` | Notes application | User note text |

Kernel reads use `fs_kernel_read_file` with an explicit length so a corrupt or
truncated file cannot overrun a caller's buffer. `UI.CFG` is read as a
fixed-size record and each byte is range-checked before use.

## Built-in content

`include/apk_samples.h` embeds sample APK payloads that are seeded into the VFS
at first mount, so a fresh image has something to demonstrate. They are byte
arrays in `.rodata`, which is why they are kept small: they consume space in the
same 0x30000-bounded image as everything else.

## Boot-media filesystem

The boot floppy image reserves LBA 398–2446 for the filesystem and LBA 2447+ for
staged packages. Packages are read directly from fixed sectors by stage 1, not
through FAT32, because the bootloader must load them before any filesystem driver
exists. This is why adding a staged package means editing `stage1.asm` and the
image layout rather than dropping a file on a volume.

## What is not here

No permissions or ownership. No journaling, no crash consistency guarantees
beyond FAT32's own. No symlinks, no hard links. No file locking. Reads and writes
are synchronous.
