#!/data/data/com.termux/files/usr/bin/bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP_DIR="${PREFIX:-/tmp}/tmp"
mkdir -p "$TMP_DIR"
CMD="${1:-start}"
PRESET="${FLAIROS_PRESET:-desktop}"

stop_one() {
  local name="$1"
  local pf="$TMP_DIR/qemu-vnc-$name.pid"
  if [ -f "$pf" ] && kill -0 "$(cat "$pf")" 2>/dev/null; then
    kill "$(cat "$pf")" || true
  fi
  rm -f "$pf"
}

case "$CMD" in
  start)
    cd "$ROOT"
    "$ROOT/tools/run-vnc.sh" stop >/dev/null 2>&1 || true
    stop_one i386
    stop_one x86_64
    stop_one aarch64
    stop_one bridge
    stop_one mobile-a64
    stop_one tv-a64

    make -j4 all storage-image >/dev/null
    python3 "$ROOT/tools/build_preset.py" --all >/dev/null

    # 0) Termux Host Bridge -> 127.0.0.1:5910
    nohup python3 "$ROOT/tools/qemu_termux_bridge.py" >"$TMP_DIR/qemu-vnc-bridge.log" 2>&1 &
    echo $! > "$TMP_DIR/qemu-vnc-bridge.pid"
    sleep 0.5

    # 1) i386 -> 127.0.0.1:5900 (:0)
    nohup qemu-system-i386 \
      -fda build/textile.img -hda build/storage.img \
      -vga std -global VGA.vgamem_mb=64 \
      -vnc 0.0.0.0:0 -k en-us \
      -serial "file:$TMP_DIR/qemu-vnc-i386.log" \
      -nic user,model=rtl8139 -no-reboot -m 256M \
      >"$TMP_DIR/qemu-vnc-i386.out" 2>&1 &
    echo $! > "$TMP_DIR/qemu-vnc-i386.pid"

    # 2) x86_64 -> 127.0.0.1:5901 (:1)
    nohup qemu-system-x86_64 \
      -kernel "build-presets/$PRESET-x86_64/flairos-$PRESET-x86_64.mb.elf" \
      -smp 4 -vga std -vnc 0.0.0.0:1 -k en-us \
      -serial "file:$TMP_DIR/qemu-vnc-x86_64.log" \
      -chardev socket,id=bridge0,host=127.0.0.1,port=5910 -serial chardev:bridge0 \
      -no-reboot -m "${FLAIROS_RAM_X86_64:-512M}" \
      >"$TMP_DIR/qemu-vnc-x86_64.out" 2>&1 &
    echo $! > "$TMP_DIR/qemu-vnc-x86_64.pid"

    # 3) aarch64 -> 127.0.0.1:5902 (:2)
    [ -f "$ROOT/build/storage-a64.img" ] || python3 "$ROOT/tools/make_storage.py" "$ROOT/build/storage-a64.img" >/dev/null
    nohup qemu-system-aarch64 \
      -M virt -cpu max -smp 4 -m "${FLAIROS_RAM_AARCH64:-2G}" \
      -device ramfb -device virtio-gpu-device \
      -drive if=none,id=hd0,format=raw,file=build/storage-a64.img -device virtio-blk-device,drive=hd0 \
      -device virtio-tablet-device -device virtio-keyboard-device \
      -kernel "build-presets/$PRESET-aarch64/flairos-$PRESET-aarch64.elf" \
      -vnc 0.0.0.0:2 -k en-us \
      -serial "file:$TMP_DIR/qemu-vnc-aarch64.log" \
      -qmp "unix:$TMP_DIR/qemu-qmp-aarch64.sock,server=on,wait=off" \
      -no-reboot \
      >"$TMP_DIR/qemu-vnc-aarch64.out" 2>&1 &
    echo $! > "$TMP_DIR/qemu-vnc-aarch64.pid"

    # 4) mobile-aarch64 -> 127.0.0.1:5903 (:3)
    nohup qemu-system-aarch64 \
      -M virt -cpu max -smp 2 -m "${FLAIROS_RAM_MOBILE:-256M}" \
      -device ramfb -device virtio-tablet-device -device virtio-keyboard-device \
      -kernel "build-presets/mobile-aarch64/flairos-mobile-aarch64.elf" \
      -vnc 0.0.0.0:3 -k en-us \
      -serial "file:$TMP_DIR/qemu-vnc-mobile-a64.log" \
      -qmp "unix:$TMP_DIR/qemu-qmp-mobile-a64.sock,server=on,wait=off" -no-reboot \
      >"$TMP_DIR/qemu-vnc-mobile-a64.out" 2>&1 &
    echo $! > "$TMP_DIR/qemu-vnc-mobile-a64.pid"

    # 5) tv-aarch64 -> 127.0.0.1:5904 (:4)
    nohup qemu-system-aarch64 \
      -M virt -cpu max -smp 2 -m "${FLAIROS_RAM_TV:-256M}" \
      -device ramfb -device virtio-tablet-device -device virtio-keyboard-device \
      -kernel "build-presets/tv-aarch64/flairos-tv-aarch64.elf" \
      -vnc 0.0.0.0:4 -k en-us \
      -serial "file:$TMP_DIR/qemu-vnc-tv-a64.log" \
      -qmp "unix:$TMP_DIR/qemu-qmp-tv-a64.sock,server=on,wait=off" -no-reboot \
      >"$TMP_DIR/qemu-vnc-tv-a64.out" 2>&1 &
    echo $! > "$TMP_DIR/qemu-vnc-tv-a64.pid"

    sleep 2
    for arch in i386 x86_64 aarch64 mobile-a64 tv-a64; do
      pf="$TMP_DIR/qemu-vnc-$arch.pid"
      if kill -0 "$(cat "$pf")" 2>/dev/null; then
        echo "OK $arch PID=$(cat "$pf")"
      else
        echo "FAIL $arch"
        cat "$TMP_DIR/qemu-vnc-$arch.out" || true
      fi
    done
    ;;
  stop)
    stop_one i386
    stop_one x86_64
    stop_one aarch64
    stop_one bridge
    stop_one mobile-a64
    stop_one tv-a64
    echo "Stopped all FlairOS VNC servers"
    ;;
  status)
    for arch in i386 x86_64 aarch64 mobile-a64 tv-a64; do
      pf="$TMP_DIR/qemu-vnc-$arch.pid"
      if [ -f "$pf" ] && kill -0 "$(cat "$pf")" 2>/dev/null; then
        echo "RUNNING $arch PID=$(cat "$pf")"
      else
        echo "STOPPED $arch"
      fi
    done
    ;;
  *)
    echo "Usage: $0 {start|stop|status}"
    exit 2
    ;;
esac
