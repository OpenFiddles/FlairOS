#!/data/data/com.termux/files/usr/bin/bash
# Run the AArch64 Desktop guest in Termux:X11.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP_DIR="${PREFIX:-/tmp}/tmp"
mkdir -p "$TMP_DIR"
cd "$ROOT"

# Build only the requested AArch64 Desktop target.
python3 tools/build_preset.py --preset desktop --arch aarch64

[ -f build/storage-a64.img ] || python3 tools/make_storage.py "$ROOT/build/storage-a64.img" >/dev/null

export XDG_RUNTIME_DIR="$TMP_DIR"
export DISPLAY=:0
if ! pgrep -f '[t]ermux-x11 :0' >/dev/null 2>&1; then
  termux-x11 :0 >"$TMP_DIR/termux-x11.log" 2>&1 &
  sleep 2
fi

if [ -n "${WINDOWED:-}" ]; then FULLSCREEN=off; else FULLSCREEN=on; fi

echo "Booting FlairOS AArch64 Desktop in Termux:X11"
exec qemu-system-aarch64 \
  -M virt -cpu max -smp "${FLAIROS_CPUS_AARCH64:-4}" -m "${FLAIROS_RAM_AARCH64:-2G}" \
  -device ramfb -device virtio-gpu-device \
  -drive "if=none,id=hd0,format=raw,file=build/storage-a64.img" \
  -device virtio-blk-device,drive=hd0 \
  -device virtio-tablet-device -device virtio-keyboard-device \
  -kernel "build-presets/desktop-aarch64/flairos-desktop-aarch64.elf" \
  -display "sdl,full-screen=$FULLSCREEN" \
  -serial "file:$TMP_DIR/flairos-aarch64-serial.log" \
  -no-reboot
