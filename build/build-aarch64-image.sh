#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROFILE="${1:-debug}"
case "$PROFILE" in debug|release) ;; *) echo "Usage: $0 [debug|release]" >&2; exit 2 ;; esac

if [ "$(uname -m)" != "aarch64" ]; then
  echo "AArch64 images must be built on a native ARM64 runner (found $(uname -m))." >&2
  exit 1
fi

OUT="${BUILD_DIR:-$ROOT/build-out-aarch64}"
WORK="$OUT/work-shared"
KERNEL_WORK="$OUT/work-$PROFILE"
SRC="$WORK/src"
KERNEL_SRC="$KERNEL_WORK/src"
TOOLS="$WORK/tools"
ROOTFS="$KERNEL_WORK/rootfs"
IMAGE="$OUT/flairos-aarch64-$PROFILE.img"
GRUB_PREFIX="$TOOLS/grub-aarch64-efi"
GRUB_MODULES="$GRUB_PREFIX/lib/grub/arm64-efi"
JOBS="${JOBS:-$(nproc)}"

mkdir -p "$SRC" "$KERNEL_SRC" "$TOOLS" "$ROOTFS" "$OUT" "$HOME/.cache/ccache"
export CCACHE_DIR="$HOME/.cache/ccache"
ccache -M 1G >/dev/null 2>&1 || true
for cmd in git make gcc ld objcopy tar gzip cpio parted losetup mkfs.vfat mkfs.ext2 mount umount ccache; do
  command -v "$cmd" >/dev/null || { echo "Missing build dependency: $cmd" >&2; exit 1; }
done
command -v sudo >/dev/null || { echo "sudo is required for disk image assembly" >&2; exit 1; }

sync_source() {
  local url="$1" dir="$2"
  if [ -d "$dir/.git" ]; then
    git -C "$dir" fetch --depth=1 origin
    git -C "$dir" reset --hard FETCH_HEAD
  else
    rm -rf "$dir"
    git clone --depth=1 "$url" "$dir"
  fi
}

echo "==> Fetching upstream Linux, GNU GRUB, and BusyBox sources"
sync_source https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git "$KERNEL_SRC/linux"
sync_source https://git.savannah.gnu.org/git/grub.git "$SRC/grub"
sync_source https://git.busybox.net/busybox "$SRC/busybox"

echo "==> Building the AArch64 Linux kernel"
make -C "$KERNEL_SRC/linux" ARCH=arm64 defconfig
(
  cd "$KERNEL_SRC/linux"
  scripts/config --enable BLK_DEV_INITRD
  scripts/config --enable BINFMT_ELF
  scripts/config --enable PCI
  scripts/config --enable VIRTIO
  scripts/config --enable VIRTIO_MMIO
  scripts/config --enable VIRTIO_BLK
  scripts/config --enable EFI_PARTITION
  scripts/config --enable EXT2_FS
  scripts/config --enable FAT_FS
  scripts/config --enable VFAT_FS
  scripts/config --enable PROC_FS
  scripts/config --enable SYSFS
  scripts/config --enable DEVTMPFS
  scripts/config --enable DEVTMPFS_MOUNT
  scripts/config --enable TMPFS
  scripts/config --enable EFI
  scripts/config --enable EFI_STUB
  scripts/config --enable SERIAL_AMBA_PL011
  scripts/config --enable SERIAL_AMBA_PL011_CONSOLE
  scripts/config --disable MODULES
  if [ "$PROFILE" = debug ]; then
    scripts/config --enable DEBUG_KERNEL
    scripts/config --enable DEBUG_INFO
  else
    scripts/config --disable DEBUG_INFO
  fi
  make ARCH=arm64 olddefconfig
)
make -C "$KERNEL_SRC/linux" ARCH=arm64 CC="ccache gcc" -j"$JOBS" Image
KERNEL="$KERNEL_SRC/linux/arch/arm64/boot/Image"
test -s "$KERNEL"

echo "==> Building static native AArch64 BusyBox userspace"
make -C "$SRC/busybox" defconfig
sed -i \
  -e 's/^# CONFIG_STATIC is not set/CONFIG_STATIC=y/' \
  -e 's/^CONFIG_TC=y/# CONFIG_TC is not set/' \
  "$SRC/busybox/.config"
grep -q '^CONFIG_STATIC=y' "$SRC/busybox/.config"
grep -q '^# CONFIG_TC is not set' "$SRC/busybox/.config"
make -C "$SRC/busybox" CC="ccache gcc" -j"$JOBS"
rm -rf "$ROOTFS"
mkdir -p "$ROOTFS"
make -C "$SRC/busybox" CONFIG_PREFIX="$ROOTFS" install
mkdir -p "$ROOTFS"/{dev,proc,sys,tmp,root,etc,sbin,usr/bin,usr/share/flairos,opt/flairos/apps,var/lib/airp/packages}
chmod 1777 "$ROOTFS/tmp"
rm -f "$ROOTFS/sbin/init"
install -m 0755 "$ROOT/system/init" "$ROOTFS/sbin/init"
test -f "$ROOTFS/sbin/init" && test ! -L "$ROOTFS/sbin/init" && test -x "$ROOTFS/sbin/init"
install -m 0755 "$ROOT/system/bin/airp" "$ROOTFS/usr/bin/airp"
install -m 0644 "$ROOT/system/etc/os-release" "$ROOTFS/etc/os-release"
printf 'FlairOS V2 minimal AArch64 userspace\n' > "$ROOTFS/usr/share/flairos/motd"
mkdir -p "$ROOTFS/boot"
install -m 0644 "$KERNEL" "$ROOTFS/boot/Image"
sudo mknod -m 600 "$ROOTFS/dev/console" c 5 1
sudo mknod -m 666 "$ROOTFS/dev/null" c 1 3
sudo chown -R "$(id -u):$(id -g)" "$ROOTFS"

echo "==> Building GNU GRUB for the ARM64 UEFI platform"
(
  cd "$SRC/grub"
  ./bootstrap
  CC="ccache gcc" TARGET_CC="ccache gcc" TARGET_OBJCOPY=objcopy \
    TARGET_STRIP=strip TARGET_NM=nm TARGET_RANLIB=ranlib \
    ./configure --target=aarch64 --with-platform=efi --prefix="$GRUB_PREFIX" \
      --disable-werror --disable-grub-mkfont --disable-device-mapper \
      --disable-efiemu --disable-grub-mount --disable-nls
  make -j"$JOBS"
  make install
)
GRUB_MKIMAGE="$GRUB_PREFIX/bin/grub-mkimage"
test -x "$GRUB_MKIMAGE"
test -d "$GRUB_MODULES"

BOOT_COMPONENTS="$OUT/boot-components"
EFI_IMAGE="$BOOT_COMPONENTS/BOOTAA64.EFI"
GRUB_CONFIG="$BOOT_COMPONENTS/grub.cfg"
mkdir -p "$BOOT_COMPONENTS/EFI/BOOT"
"$GRUB_MKIMAGE" -O arm64-efi -o "$EFI_IMAGE" -p /EFI/BOOT -d "$GRUB_MODULES" \
  part_gpt fat ext2 normal linux configfile search search_fs_uuid
cat > "$GRUB_CONFIG" <<'GRUBCFG'
set timeout=3
set default=0
menuentry "FlairOS V2 ARM64" {
    insmod part_gpt
    insmod ext2
    set root=(hd0,gpt2)
    linux /boot/Image root=/dev/vda2 rootfstype=ext2 rootwait rw console=ttyAMA0,115200n8
}
GRUBCFG
install -m 0644 "$KERNEL" "$BOOT_COMPONENTS/Image"
install -m 0644 "$GRUB_CONFIG" "$BOOT_COMPONENTS/EFI/BOOT/grub.cfg"
test -s "$EFI_IMAGE" && test -s "$BOOT_COMPONENTS/Image"
"$GRUB_PREFIX/bin/grub-file" --is-arm64-efi "$EFI_IMAGE"

echo "==> Creating a GPT disk with an EFI System Partition and ext2 root"
rm -f "$IMAGE"
truncate -s 768M "$IMAGE"
parted -s "$IMAGE" mklabel gpt
parted -s "$IMAGE" mkpart ESP fat32 1MiB 129MiB
parted -s "$IMAGE" set 1 esp on
parted -s "$IMAGE" mkpart primary ext2 129MiB 100%
LOOP="$(sudo losetup --find --show --partscan "$IMAGE")"
BOOT_PART="${LOOP}p1"
ROOT_PART="${LOOP}p2"
MOUNT="$KERNEL_WORK/mount"
mkdir -p "$MOUNT"
cleanup() {
  set +e
  sudo umount "$MOUNT" 2>/dev/null
  sudo losetup -d "$LOOP" 2>/dev/null
}
trap cleanup EXIT

for _ in $(seq 1 30); do
  [ -b "$BOOT_PART" ] && [ -b "$ROOT_PART" ] && break
  sleep 1
done
[ -b "$BOOT_PART" ] && [ -b "$ROOT_PART" ] || { echo "Partition devices did not appear for $LOOP" >&2; exit 1; }
sudo mkfs.vfat -F 32 -n FLAIROS_EFI "$BOOT_PART"
sudo mkfs.ext2 -F -L FLAIROS_ROOT -d "$ROOTFS" "$ROOT_PART"
sudo mount "$BOOT_PART" "$MOUNT"
sudo mkdir -p "$MOUNT/EFI/BOOT"
sudo cp "$EFI_IMAGE" "$MOUNT/EFI/BOOT/BOOTAA64.EFI"
sudo cp "$GRUB_CONFIG" "$MOUNT/EFI/BOOT/grub.cfg"
sync
sudo umount "$MOUNT"
sudo losetup -d "$LOOP"
trap - EXIT

echo "Built $IMAGE"
ls -lh "$IMAGE" "$EFI_IMAGE" "$BOOT_COMPONENTS/Image"
