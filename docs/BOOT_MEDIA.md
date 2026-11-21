# FlairOS V2 Boot Media

The Linux foundation produces one raw, BIOS-bootable i386 disk image.

## Disk layout

- MBR partition table and GRUB BIOS boot code
- Partition 1: FAT32, labeled `FLAIROS_BOOT`, containing `/boot/bzImage` and GRUB configuration
- Partition 2: ext2, labeled `FLAIROS_ROOT`, containing the minimal FlairOS userspace

GRUB loads the Linux kernel from the FAT32 partition. Linux mounts the ext2 root partition at `/dev/sda2` and starts `/sbin/init`.

## Outputs

- `build-out/flairos-i386-debug.img`
- `build-out/flairos-i386-release.img`

GitHub Actions uploads each image as a workflow artifact. The debug and release outputs are separate.
