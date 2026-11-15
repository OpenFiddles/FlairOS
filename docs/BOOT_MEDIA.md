# FlairOS Boot Media

## Images

- `make boot-media` builds both optical images.
- `build/livecd.iso` boots directly into the existing FlairOS desktop in Live mode.
- `build/bootcd.iso` boots into the non-destructive FlairOS Setup center.
- `build/textile.img` remains the legacy normal-boot floppy image.

The browser emulator boots `flair-livecd.iso` through a virtual CD-ROM drive rather than using the legacy floppy image.

## Live session guarantees

Live mode initializes the existing flat filesystem as a RAM-only session. It does not read the on-disk filesystem directory, does not commit filesystem changes to ATA, and refuses the `format` operation. Files created during the session are temporary and disappear on reboot.

The filesystem APIs `fs_is_volatile()` and `fs_disk_writes_enabled()` report the active mode. Their syscall IDs are 35 and 36.

## Setup center

The BootCD setup center can start a Live session, inspect detected legacy ATA devices, display installation readiness, and show basic diagnostics. The installer intentionally does not partition or format disks yet. Read-only partition-table validation, a tested FlairFS, and a transactional installer are prerequisites before installation can be enabled.

## Boot handoff

Stage 2 writes a versioned handoff at physical address `0x4FF0`; the E820 memory map begins at `0x5000`, so the handoff does not overlap it. Invalid or missing handoffs fall back to normal boot. This is a small mode-selection hook only; the broader bootloader redesign remains a separate follow-up.
