#!/usr/bin/env bash
# flairos-start.sh - bootstrap and start the FlairOS 64-bit line on a Linux host.
#
# The repo's real 64-bit build path is tools/build_preset.py (clang + ld.lld),
# which CMake does not drive. tools/run-vnc-multi.sh and tools/run-x11-aarch64.sh
# are Termux scripts (Termux shebang, $PREFIX, termux-x11) and do not run on a
# plain Linux box or VM. This script wraps the preset path for Linux.
#
# Usage:
#   ./flairos-start.sh                 # deps -> build -> verify desktop
#   ./flairos-start.sh deps            # install toolchain only
#   ./flairos-start.sh build           # build all presets, both arches
#   ./flairos-start.sh verify [preset] # headless boot + render=ok check
#   ./flairos-start.sh verify-all      # headless boot, all 6 preset/arch pairs
#   ./flairos-start.sh vnc start|stop|status
#   ./flairos-start.sh i386            # i386 regression image (not the product)
#
# Env:
#   FLAIROS_RUN_DIR  logs/pids/screenshots (default /tmp/flairos-run)
#   BOOT_TIMEOUT     seconds to wait for render=ok (default 120; TCG is slow)
#   FLAIROS_PRESET   desktop|mobile|tv (default desktop)

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

RUN_DIR="${FLAIROS_RUN_DIR:-${TMPDIR:-/tmp}/flairos-run}"
LOG_DIR="$RUN_DIR/logs"
PID_DIR="$RUN_DIR/pids"
IMG_DIR="$RUN_DIR/shots"
BOOT_TIMEOUT="${BOOT_TIMEOUT:-120}"
PRESET="${FLAIROS_PRESET:-desktop}"
mkdir -p "$LOG_DIR" "$PID_DIR" "$IMG_DIR"

log()  { printf '\033[0;36m[start]\033[0m %s\n' "$*"; }
ok()   { printf '\033[0;32m[ ok  ]\033[0m %s\n' "$*"; }
warn() { printf '\033[0;33m[warn ]\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[0;31m[fail ]\033[0m %s\n' "$*" >&2; exit 1; }

# Acceleration is probed per arch: aarch64 guests can only use KVM on an
# aarch64 host, so on x86 they always run under TCG.
ACCEL_X86="tcg"
ACCEL_ARM="tcg"

accel_description() { echo "x86_64=$ACCEL_X86 aarch64=$ACCEL_ARM"; }

probe_accel() {
  local arch_out
  [ -r /dev/kvm ] && [ -w /dev/kvm ] || return 0
  command -v qemu-system-x86_64 >/dev/null 2>&1 || return 0

  # "-machine none" starts and idles; timeout 5 kills it, so any diagnostic
  # output tells us whether KVM really initialised.
  arch_out="$(timeout 5 qemu-system-x86_64 -accel kvm -machine none \
      -display none -monitor none -serial none </dev/null 2>&1)" || true
  case "$arch_out" in
    *"Could not access KVM"*|*"invalid accelerator"*|*"KVM is not supported"*) return 0 ;;
  esac

  ACCEL_X86="kvm"
  [ "$(uname -m)" = "aarch64" ] && ACCEL_ARM="kvm"
  return 0
}

# Rootless fallback prefix (used when apt/sudo are unavailable).
TOOLCHAIN="${FLAIROS_TOOLCHAIN:-$HOME/local}"
QEMU_BIOS=()

# Put a usable clang/lld/qemu on PATH. Returns 0 when the full set is present.
set_toolchain_env() {
  local t
  local bindir=""
  if [ -x "$TOOLCHAIN/bin/clang" ] && [ -x "$TOOLCHAIN/bin/qemu-system-x86_64" ]; then
    bindir="$TOOLCHAIN/bin"
    export LD_LIBRARY_PATH="$TOOLCHAIN/lib/x86_64-linux-gnu:$TOOLCHAIN/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    QEMU_BIOS=(-L "$TOOLCHAIN/usr/share/qemu")
    export PATH="$bindir:$PATH"
  fi
  for t in clang ld.lld llvm-objcopy python3 qemu-system-x86_64 qemu-system-aarch64; do
    command -v "$t" >/dev/null 2>&1 || return 1
  done
  probe_accel
  return 0
}

# ---------------------------------------------------------------------------
deps() {
  log "checking toolchain (system packages do not survive a sandbox restart)"
  if set_toolchain_env; then
    ok "toolchain already available ($(accel_description), firmware=${QEMU_BIOS[*]:-system default})"
  elif [ "$(id -u)" -eq 0 ] || { command -v sudo >/dev/null 2>&1 && sudo -n true 2>/dev/null; }; then
    local SUDO=""
    [ "$(id -u)" -ne 0 ] && SUDO="sudo"
    export DEBIAN_FRONTEND=noninteractive
    $SUDO apt-get update -qq
    $SUDO apt-get install -y -qq \
      nasm gcc-multilib binutils make python3 python3-pil \
      cmake ninja-build clang lld llvm \
      qemu-system-x86 qemu-system-arm xorriso >/dev/null
    ok "packages installed via apt"
    set_toolchain_env || die "toolchain incomplete after apt install"
  else
    warn "no root and no usable apt; installing the toolchain without root"
    python3 tools/install-toolchain-rootless.py --prefix "$TOOLCHAIN" 2>&1 | tail -25
    set_toolchain_env || die "rootless toolchain install failed"
    ok "rootless toolchain ready at $TOOLCHAIN"
  fi

  # verify_matrix.py / the screendump conversion need Pillow; it is optional.
  python3 -c 'import PIL' 2>/dev/null \
    || pip3 install --quiet --break-system-packages pillow 2>/dev/null \
    || warn "Pillow unavailable; screenshots will stay as PPM"

  for t in clang ld.lld llvm-objcopy python3 qemu-system-x86_64 qemu-system-aarch64; do
    command -v "$t" >/dev/null 2>&1 || die "missing: $t"
  done
  ok "all required tools present ($(accel_description))"
}

# ---------------------------------------------------------------------------
build() {
  log "building presets: desktop/mobile/tv for x86_64 + aarch64"
  python3 tools/build_preset.py --all 2>&1 | tee "$LOG_DIR/build.log"
  local miss=0 p a f
  for p in desktop mobile tv; do
    for a in x86_64 aarch64; do
      if [ "$a" = x86_64 ]; then
        f="build-presets/$p-$a/flairos-$p-$a.mb.elf"
      else
        f="build-presets/$p-$a/flairos-$p-$a.elf"
      fi
      [ -s "$f" ] || { warn "missing $f"; miss=1; }
    done
  done
  [ "$miss" -eq 0 ] || die "one or more preset images were not produced"
  ok "all preset images built"
}

# ---------------------------------------------------------------------------
# Send HMP commands to a running QEMU over its monitor unix socket.
monitor_send() {
  local sock="$1"; shift
  python3 - "$sock" "$@" <<'PY'
import socket, sys, time
path, cmds = sys.argv[1], sys.argv[2:]
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.settimeout(5.0)
s.connect(path)
time.sleep(0.2)
try:
    s.recv(65536)
except Exception:
    pass
for c in cmds:
    s.sendall((c + "\n").encode())
    time.sleep(0.3)
    try:
        s.recv(65536)
    except Exception:
        pass
s.close()
PY
}

wait_for_render() {
  local ser="$1" pid="$2" timeout="$3"
  local w=0
  while [ "$w" -lt "$timeout" ]; do
    grep -q "render=ok" "$ser" 2>/dev/null && { echo "$w"; return 0; }
    kill -0 "$pid" 2>/dev/null || { echo "$w"; return 1; }
    sleep 1; w=$((w + 1))
  done
  echo "$w"; return 1
}

boot_headless() {
  local preset="$1" arch="$2"
  local ser="$LOG_DIR/${preset}-${arch}-serial.log"
  local ppm="$IMG_DIR/${preset}-${arch}.ppm"
  local sock="$RUN_DIR/qemu-${preset}-${arch}.sock"
  local qout="$LOG_DIR/${preset}-${arch}-qemu.out"
  rm -f "$ser" "$ppm" "$sock" "$qout"

  local -a cmd
  if [ "$arch" = x86_64 ]; then
    local kern="build-presets/$preset-$arch/flairos-$preset-$arch.mb.elf"
    [ -s "$kern" ] || { warn "missing kernel: $kern"; return 1; }
    cmd=(qemu-system-x86_64 -accel "$ACCEL_X86" "${QEMU_BIOS[@]}" -kernel "$kern"
         -vga std -m 512M -display none -no-reboot
         -serial "file:$ser" -monitor "unix:$sock,server=on,wait=off")
  else
    local kern="build-presets/$preset-$arch/flairos-$preset-$arch.elf"
    [ -s "$kern" ] || { warn "missing kernel: $kern"; return 1; }
    cmd=(qemu-system-aarch64 -accel "$ACCEL_ARM" "${QEMU_BIOS[@]}" -M virt -cpu max -m 1024M
         -device ramfb -device virtio-tablet-device -device virtio-keyboard-device
         -kernel "$kern" -display none -no-reboot
         -serial "file:$ser" -monitor "unix:$sock,server=on,wait=off")
  fi

  log "booting $preset/$arch (timeout ${BOOT_TIMEOUT}s)"
  "${cmd[@]}" >"$qout" 2>&1 &
  local qpid=$!

  local waited ready=0
  if waited="$(wait_for_render "$ser" "$qpid" "$BOOT_TIMEOUT")"; then ready=1; fi

  if [ "$ready" -eq 1 ] && [ -S "$sock" ]; then
    monitor_send "$sock" "screendump $ppm" "quit" || true
  elif kill -0 "$qpid" 2>/dev/null; then
    [ -S "$sock" ] && monitor_send "$sock" "quit" || true
  fi

  local t=0
  while kill -0 "$qpid" 2>/dev/null && [ "$t" -lt 15 ]; do sleep 1; t=$((t + 1)); done
  kill -0 "$qpid" 2>/dev/null && kill -9 "$qpid" 2>/dev/null || true
  wait "$qpid" 2>/dev/null || true

  if [ "$ready" -eq 1 ]; then
    ok "$preset/$arch reached render=ok after ${waited}s"
    grep -E "arch=|preset=|render=|ramfb|bochs|vmmouse|ps2 mouse" "$ser" 2>/dev/null | sed 's/^/       /' || true
    if [ -s "$ppm" ]; then
      if python3 -c 'import PIL' 2>/dev/null; then
        python3 - "$ppm" "$IMG_DIR/${preset}-${arch}.png" <<'PY' || true
import sys
from PIL import Image
Image.open(sys.argv[1]).save(sys.argv[2])
PY
        [ -s "$IMG_DIR/${preset}-${arch}.png" ] && ok "screenshot -> $IMG_DIR/${preset}-${arch}.png"
      else
        ok "screenshot -> $ppm (Pillow absent, left as PPM)"
      fi
    else
      warn "no screendump produced for $preset/$arch"
    fi
    return 0
  fi

  warn "$preset/$arch did NOT reach render=ok in ${waited}s"
  [ -s "$ser" ] && tail -30 "$ser" | sed 's/^/       /'
  [ -s "$qout" ] && { echo "       --- qemu stderr ---"; tail -10 "$qout" | sed 's/^/       /'; }
  return 1
}

verify() {
  local preset="${1:-$PRESET}"
  local fail=0 a
  for a in x86_64 aarch64; do
    boot_headless "$preset" "$a" || fail=$((fail + 1))
  done
  if [ "$fail" -ne 0 ]; then
    warn "$fail of 2 boots failed for preset '$preset' (logs in $LOG_DIR)"
    return 1
  fi
  ok "both $preset boots reached render=ok"
}

verify_all() {
  local fail=0 p
  for p in desktop mobile tv; do
    for a in x86_64 aarch64; do
      boot_headless "$p" "$a" || fail=$((fail + 1))
    done
  done
  [ "$fail" -eq 0 ] || { warn "$fail of 6 boots failed"; return 1; }
  ok "all 6 preset/arch boots reached render=ok"
}

# ---------------------------------------------------------------------------
vnc_stop_one() {
  local pf="$PID_DIR/$1.pid"
  if [ -f "$pf" ] && kill -0 "$(cat "$pf")" 2>/dev/null; then
    kill "$(cat "$pf")" 2>/dev/null || true
  fi
  rm -f "$pf"
}

vnc_start() {
  command -v qemu-system-x86_64 >/dev/null || die "run './flairos-start.sh deps' first"
  build
  local -a names=(x86_64 aarch64 mobile-a64 tv-a64)
  local -a logs=(vnc-x86_64.log vnc-aarch64.log vnc-mobile-a64.log vnc-tv-a64.log)
  local n
  for n in "${names[@]}"; do vnc_stop_one "$n"; done

  [ -f build/storage-a64.img ] || python3 tools/make_storage.py build/storage-a64.img >/dev/null

  log "x86_64 desktop  -> vnc :5901"
  nohup qemu-system-x86_64 -accel "$ACCEL_X86" "${QEMU_BIOS[@]}" \
    -kernel "build-presets/$PRESET-x86_64/flairos-$PRESET-x86_64.mb.elf" \
    -smp 4 -vga std -m 512M -no-reboot \
    -vnc 0.0.0.0:1 -k en-us -serial "file:$LOG_DIR/vnc-x86_64.log" \
    >"$LOG_DIR/vnc-x86_64.out" 2>&1 &
  echo $! > "$PID_DIR/x86_64.pid"

  log "aarch64 desktop -> vnc :5902"
  nohup qemu-system-aarch64 -accel "$ACCEL_ARM" "${QEMU_BIOS[@]}" \
    -M virt -cpu max -smp 4 -m 2G -no-reboot \
    -device ramfb -device virtio-gpu-device \
    -drive if=none,id=hd0,format=raw,file=build/storage-a64.img \
    -device virtio-blk-device,drive=hd0 \
    -device virtio-tablet-device -device virtio-keyboard-device \
    -kernel "build-presets/$PRESET-aarch64/flairos-$PRESET-aarch64.elf" \
    -vnc 0.0.0.0:2 -k en-us -serial "file:$LOG_DIR/vnc-aarch64.log" \
    >"$LOG_DIR/vnc-aarch64.out" 2>&1 &
  echo $! > "$PID_DIR/aarch64.pid"

  log "aarch64 mobile  -> vnc :5903"
  nohup qemu-system-aarch64 -accel "$ACCEL_ARM" "${QEMU_BIOS[@]}" \
    -M virt -cpu max -smp 2 -m 256M -no-reboot \
    -device ramfb -device virtio-tablet-device -device virtio-keyboard-device \
    -kernel "build-presets/mobile-aarch64/flairos-mobile-aarch64.elf" \
    -vnc 0.0.0.0:3 -k en-us -serial "file:$LOG_DIR/vnc-mobile-a64.log" \
    >"$LOG_DIR/vnc-mobile-a64.out" 2>&1 &
  echo $! > "$PID_DIR/mobile-a64.pid"

  log "aarch64 tv      -> vnc :5904"
  nohup qemu-system-aarch64 -accel "$ACCEL_ARM" "${QEMU_BIOS[@]}" \
    -M virt -cpu max -smp 2 -m 256M -no-reboot \
    -device ramfb -device virtio-tablet-device -device virtio-keyboard-device \
    -kernel "build-presets/tv-aarch64/flairos-tv-aarch64.elf" \
    -vnc 0.0.0.0:4 -k en-us -serial "file:$LOG_DIR/vnc-tv-a64.log" \
    >"$LOG_DIR/vnc-tv-a64.out" 2>&1 &
  echo $! > "$PID_DIR/tv-a64.pid"

  log "waiting for guests to render (up to ${BOOT_TIMEOUT}s each)"
  local i w
  for i in "${!names[@]}"; do
    w="$(wait_for_render "$LOG_DIR/${logs[$i]}" "$(cat "$PID_DIR/${names[$i]}.pid")" "$BOOT_TIMEOUT")" || true
    if grep -q "render=ok" "$LOG_DIR/${logs[$i]}" 2>/dev/null; then
      ok "${names[$i]} rendered in ${w}s (PID $(cat "$PID_DIR/${names[$i]}.pid"))"
    else
      warn "${names[$i]} not rendering after ${w}s - see $LOG_DIR/${logs[$i]}"
    fi
  done

  echo
  echo "Screenshots: python3 tools/vnc_ctl.py screenshot 5902 $IMG_DIR/desktop.png"
  echo "Pointer:     python3 tools/vnc_ctl.py click 5902 235 715 --out $IMG_DIR/click.png"
  echo "Stop:        ./flairos-start.sh vnc stop"
}

vnc_status() {
  local n pf
  for n in x86_64 aarch64 mobile-a64 tv-a64; do
    pf="$PID_DIR/$n.pid"
    if [ -f "$pf" ] && kill -0 "$(cat "$pf")" 2>/dev/null; then
      echo "RUNNING $n PID=$(cat "$pf")"
    else
      echo "STOPPED $n"
    fi
  done
}

# ---------------------------------------------------------------------------
i386_image() {
  log "building i386 reference image (regression only, not the product)"
  make -j"$(nproc)" all storage-image
  [ -s build/textile.img ] || die "build/textile.img missing"
  ok "i386 image: build/textile.img ($(stat -c%s build/textile.img) bytes)"
}

# ---------------------------------------------------------------------------
CMD="${1:-all}"
case "$CMD" in
  deps)       deps ;;
  build)      deps; build ;;
  verify)     deps; verify "${2:-$PRESET}" ;;
  verify-all) deps; verify_all ;;
  vnc)
    case "${2:-start}" in
      start)  vnc_start ;;
      stop)   for n in x86_64 aarch64 mobile-a64 tv-a64; do vnc_stop_one "$n"; done
              ok "stopped all VNC guests" ;;
      status) vnc_status ;;
      *) die "usage: $0 vnc {start|stop|status}" ;;
    esac
    ;;
  i386)       deps; i386_image ;;
  all)        deps; build; verify "$PRESET" ;;
  *) die "usage: $0 {all|deps|build|verify [preset]|verify-all|vnc start|vnc stop|vnc status|i386}" ;;
esac
