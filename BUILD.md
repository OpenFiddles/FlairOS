# Building FlairOS V2

The Linux builds are performed by GitHub Actions. The workflow fetches upstream source repositories with shallow clones, builds Linux, GNU GRUB, and BusyBox, then assembles architecture-specific bootable disk images. The i386 target preserves the legacy BIOS/MBR layout for v86. The AArch64 target is built natively on a GitHub-hosted ARM64 runner and uses ARM64 GRUB EFI, a GPT EFI System Partition, and an ext2 root filesystem for QEMU ‘virt’ machines.

## Local build

Install the dependencies listed in `.github/workflows/ci.yml`, then run:

```sh
bash build/build-linux-image.sh debug
bash build/build-linux-image.sh release
```

Output:

- `build-out/flairos-i386-debug.img`
- `build-out/flairos-i386-release.img`

## AArch64 / ARM64 build

GitHub Actions builds both debug and release ARM64 images on the native `ubuntu-24.04-arm` runner. The build produces an ARM64 Linux `Image`, an ARM64 UEFI GRUB executable (`BOOTAA64.EFI`), and a GPT disk image with a FAT32 EFI System Partition and ext2 root partition. The kernel includes the virtio block and PL011 serial drivers required by QEMU `virt`.

The workflow uploads these artifacts:

- `flairos-aarch64-debug`
- `flairos-aarch64-release`
- `flairos-aarch64-boot-components` (kernel, GRUB EFI executable, and GRUB configuration)

The native ARM64 image builder is available as `make aarch64-image PROFILE=debug` on an AArch64 Linux host with the dependencies in the ARM64 workflow installed.

For the Android phone, run the following from the Debian environment in Termux. Install QEMU and its ARM64 UEFI firmware first:

```sh
apt update
apt install qemu-system-arm qemu-efi-aarch64
```

This explicitly selects software emulation, so it does not depend on `/dev/kvm`:

```sh
cp /usr/share/AAVMF/AAVMF_VARS.fd vars.fd
qemu-system-aarch64 -accel tcg \
  -machine virt -cpu cortex-a57 -m 768 -smp 2 \
  -drive if=pflash,format=raw,unit=0,readonly=on,file=/usr/share/AAVMF/AAVMF_CODE.fd \
  -drive if=pflash,format=raw,unit=1,file=vars.fd \
  -drive if=none,file=build-out-aarch64/flairos-aarch64-debug.img,format=raw,id=hd0 \
  -device virtio-blk-device,drive=hd0 \
  -display none -serial stdio -monitor none
```

The i386 disk uses an MBR partition table, a FAT32 GRUB boot partition, and an ext2 root filesystem. Its Linux kernel includes the root filesystem drivers required for this layout.

## Validation

```sh
sh tests/airp-package-test.sh
qemu-system-i386 -m 256M -drive file=build-out/flairos-i386-debug.img,format=raw,if=ide
```

GitHub is the source repository. GitHub Actions builds, validates, and publishes architecture-specific artifacts.
