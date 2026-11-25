#!/data/data/com.termux/files/usr/bin/bash
# Run FlairOS in a window on this phone through Termux:X11.
#
# One-time setup (already done on this phone):
#   pkg install x11-repo
#   pkg install termux-x11-nightly qemu-system-i386      # the non-headless QEMU
#   Install the "Termux:X11" Android app (package com.termux.x11).
#
# Usage:   bash tools/run-x11.sh            build, start the X server, boot FlairOS
#          bash tools/run-x11.sh -m 512M    extra arguments go straight to QEMU
# The window opens full-screen. Pass WINDOWED=1 to get a normal window instead.
# The pointer is absolute (VMware protocol), so touch and mouse work without a grab;
# if a grab ever happens, release it with Ctrl+Alt+G.
# Serial output: $PREFIX/tmp/flairos-serial.log
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP_DIR="${PREFIX:-/tmp}/tmp"
mkdir -p "$TMP_DIR"
cd "$ROOT"

make -j4 all storage-image >/dev/null

export XDG_RUNTIME_DIR="$TMP_DIR"
export DISPLAY=:0
if ! pgrep -f 'termux-x11' >/dev/null 2>&1; then
  termux-x11 :0 >"$TMP_DIR/termux-x11.log" 2>&1 &
  sleep 3
fi
# Bring the Termux:X11 app to the front so the window is visible.
am start --user 0 -n com.termux.x11/com.termux.x11.MainActivity >/dev/null 2>&1 || true
sleep 1

if [ -n "${WINDOWED:-}" ]; then FULLSCREEN=off; else FULLSCREEN=on; fi

# The system image is always opened in snapshot mode (writes are discarded), so it never
# clashes with another QEMU. The storage disk is persistent unless the VNC session from
# tools/run-vnc.sh is still running and holding it; then it is temporary for this run.
STORAGE_OPTS=""
if pgrep -f 'qemu-system-i386.*storage.img' >/dev/null 2>&1; then
  echo "Note: another QEMU (VNC session?) is using build/storage.img; this run uses a temporary disk."
  echo "      Stop it with: bash tools/run-vnc.sh stop"
  STORAGE_OPTS=",snapshot=on"
fi

echo "Booting FlairOS in Termux:X11 (serial log: $TMP_DIR/flairos-serial.log)"
exec qemu-system-i386 \
  -drive "file=build/textile.img,format=raw,if=floppy,snapshot=on" \
  -drive "file=build/storage.img,format=raw,if=ide${STORAGE_OPTS}" \
  -vga std -global VGA.vgamem_mb=64 \
  -nic user,model=rtl8139 \
  -display "sdl,full-screen=$FULLSCREEN" \
  -serial "file:$TMP_DIR/flairos-serial.log" \
  -no-reboot \
  -m 256M \
  "$@"
