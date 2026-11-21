# Building FlairOS V2

The Linux build is performed by GitHub Actions. The workflow fetches upstream source repositories with shallow clones, builds the kernel, GNU GRUB, and BusyBox, then assembles one bootable i386 disk image.

## Local build

Install the dependencies listed in `.github/workflows/ci.yml`, then run:

```sh
./build/build-linux-image.sh debug
./build/build-linux-image.sh release
```

Output:

- `build-out/flairos-i386-debug.img`
- `build-out/flairos-i386-release.img`

The disk uses an MBR partition table, a FAT32 GRUB boot partition, and an ext2 root filesystem. The Linux kernel and root filesystem drivers required for this layout are built into the kernel.

## Validation

```sh
sh tests/airp-package-test.sh
qemu-system-i386 -m 256M -drive file=build-out/flairos-i386-debug.img,format=raw,if=ide
```

GitLab is used for source changes. GitHub Actions is the build, validation, and artifact system.
