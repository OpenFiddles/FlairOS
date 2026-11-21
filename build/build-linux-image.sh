#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROFILE="${1:-${PROFILE:-debug}}"
case "$PROFILE" in debug|release) ;; *) echo "Usage: $0 [debug|release]" >&2; exit 2 ;; esac

OUT="${BUILD_DIR:-$ROOT/build-out}"
WORK="$OUT/work-$PROFILE"
SRC="$WORK/src"
TOOLS="$WORK/tools"
ROOTFS="$WORK/rootfs"
IMAGE="$OUT/flairos-i386-$PROFILE.img"
GRUB_PREFIX="$TOOLS/grub"
JOBS="${JOBS:-$(nproc)}"

mkdir -p "$SRC" "$TOOLS" "$ROOTFS" "$OUT"
for cmd in git make gcc ld objcopy tar gzip cpio parted losetup mkfs.vfat mkfs.ext2 mount umount; do
  command -v "$cmd" >/dev/null || { echo "Missing build dependency: $cmd" >&2; exit 1; }
done
command -v sudo >/dev/null || { echo "sudo is required for disk image assembly" >&2; exit 1; }

echo "==> Fetching official Linux, GRUB, and BusyBox sources (shallow clones)"
git clone --depth=1 https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git "$SRC/linux"
git clone --depth=1 https://git.savannah.gnu.org/git/grub.git "$SRC/grub"
git clone --depth=1 https://git.busybox.net/busybox "$SRC/busybox"

echo "==> Configuring and building the minimal i386 Linux kernel"
make -C "$SRC/linux" ARCH=x86 i386_defconfig
(
  cd "$SRC/linux"
  scripts/config --enable BLK_DEV_INITRD
  scripts/config --enable BINFMT_ELF
  scripts/config --enable PCI
  scripts/config --enable ATA
  scripts/config --enable ATA_PIIX
  scripts/config --enable SCSI
  scripts/config --enable BLK_DEV_SD
  scripts/config --enable EXT2_FS
  scripts/config --enable PROC_FS
  scripts/config --enable SYSFS
  scripts/config --enable DEVTMPFS
  scripts/config --enable DEVTMPFS_MOUNT
  scripts/config --enable TMPFS
  scripts/config --enable VT
  scripts/config --enable VGA_CONSOLE
  scripts/config --enable SERIAL_8250
  scripts/config --enable SERIAL_8250_CONSOLE
  scripts/config --disable MODULES
  if [ "$PROFILE" = debug ]; then
    scripts/config --enable DEBUG_KERNEL
    scripts/config --enable DEBUG_INFO
  else
    scripts/config --disable DEBUG_INFO
  fi
  make ARCH=x86 olddefconfig
)
make -C "$SRC/linux" ARCH=x86 -j"$JOBS" bzImage
KERNEL="$SRC/linux/arch/x86/boot/bzImage"
test -s "$KERNEL"

echo "==> Building a static i386 BusyBox userspace"
make -C "$SRC/busybox" ARCH=i386 CROSS_COMPILE=i686-linux-gnu- defconfig
sed -i 's/^# CONFIG_STATIC is not set/CONFIG_STATIC=y/' "$SRC/busybox/.config"
grep -q '^CONFIG_STATIC=y' "$SRC/busybox/.config"
make -C "$SRC/busybox" ARCH=i386 CROSS_COMPILE=i686-linux-gnu- -j"$JOBS"
rm -rf "$ROOTFS"
mkdir -p "$ROOTFS"
make -C "$SRC/busybox" CONFIG_PREFIX="$ROOTFS" install
mkdir -p "$ROOTFS"/{dev,proc,sys,tmp,root,etc,sbin,usr/bin,usr/share/flairos,opt/flairos/apps,var/lib/airp/packages}
chmod 1777 "$ROOTFS/tmp"
cp "$ROOT/system/init" "$ROOTFS/sbin/init"
cp "$ROOT/system/bin/airp" "$ROOTFS/usr/bin/airp"
chmod 0755 "$ROOTFS/sbin/init" "$ROOTFS/usr/bin/airp"
cp "$ROOT/system/etc/os-release" "$ROOTFS/etc/os-release"
printf 'FlairOS V2 minimal userspace\n' > "$ROOTFS/usr/share/flairos/motd"
sudo mknod -m 600 "$ROOTFS/dev/console" c 5 1
sudo mknod -m 666 "$ROOTFS/dev/null" c 1 3
sudo chown -R "$(id -u):$(id -g)" "$ROOTFS"

echo "==> Building GRUB from the official GNU GRUB source"
(
  cd "$SRC/grub"
  ./bootstrap
  ./configure --target=i386 --with-platform=pc --prefix="$GRUB_PREFIX" \
    --disable-werror --disable-grub-mkfont --disable-device-mapper \
    --disable-efiemu --disable-grub-mount --disable-nls
  make -j"$JOBS"
  make install
)
export PATH="$GRUB_PREFIX/bin:$GRUB_PREFIX/sbin:$PATH"
GRUB_MODULES="$GRUB_PREFIX/lib/grub/i386-pc"
test -d "$GRUB_MODULES"

echo "==> Creating one bootable MBR disk image"
rm -f "$IMAGE"
truncate -s 512M "$IMAGE"
parted -s "$IMAGE" mklabel msdos
parted -s "$IMAGE" mkpart primary fat32 1MiB 128MiB
parted -s "$IMAGE" set 1 boot on
parted -s "$IMAGE" mkpart primary ext2 128MiB 100%
LOOP="$(sudo losetup --find --show --partscan "$IMAGE")"
BOOT_PART="${LOOP}p1"
ROOT_PART="${LOOP}p2"
MOUNT="$WORK/mount"
mkdir -p "$MOUNT"
cleanup() {
  set +e
  sudo umount "$MOUNT" 2>/dev/null
  sudo losetup -d "$LOOP" 2>/dev/null
}
trap cleanup EXIT

for _ in $(seq 1 20); do
  [ -b "$BOOT_PART" ] && [ -b "$ROOT_PART" ] && break
  sleep 1
done
[ -b "$BOOT_PART" ] && [ -b "$ROOT_PART" ] || { echo "Partition devices did not appear for $LOOP" >&2; exit 1; }

sudo mkfs.vfat -F 32 -n FLAIROS_BOOT "$BOOT_PART"
sudo mkfs.ext2 -F -L FLAIROS_ROOT -d "$ROOTFS" "$ROOT_PART"
sudo mount "$BOOT_PART" "$MOUNT"
sudo mkdir -p "$MOUNT/boot/grub"
sudo cp "$KERNEL" "$MOUNT/boot/bzImage"
sudo tee "$MOUNT/boot/grub/grub.cfg" >/dev/null <<'GRUBCFG'
set timeout=3
set default=0

menuentry "FlairOS V2" {
    insmod part_msdos
    insmod fat
    insmod ext2
    insmod normal
    set root=(hd0,msdos1)
    linux /boot/bzImage root=/dev/sda2 rootfstype=ext2 rootwait rw console=tty0 console=ttyS0,115200n8
}
GRUBCFG
sudo "$GRUB_PREFIX/sbin/grub-install" --target=i386-pc --directory="$GRUB_MODULES" \
  --boot-directory="$MOUNT/boot" --no-floppy "$LOOP"
sync
sudo umount "$MOUNT"
sudo losetup -d "$LOOP"
trap - EXIT

echo "Built $IMAGE"
ls -lh "$IMAGE"
