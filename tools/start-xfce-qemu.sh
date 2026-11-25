#!/data/data/com.termux/files/usr/bin/bash
# One-command launcher: Termux:X11 + XFCE + full-screen QEMU (FlairOS).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP_DIR="${PREFIX:-/tmp}/tmp"
mkdir -p "$TMP_DIR"

export XDG_RUNTIME_DIR="$TMP_DIR"
export DISPLAY=:0

# 1. Start Termux:X11 server if needed
if ! pgrep -f 'termux-x11' >/dev/null 2>&1; then
  nohup termux-x11 :0 >"$TMP_DIR/termux-x11.log" 2>&1 &
  sleep 2
fi

# 2. Start XFCE session if needed
if ! pgrep -x 'xfce4-session' >/dev/null 2>&1; then
  nohup startxfce4 >"$TMP_DIR/xfce4.log" 2>&1 &
  sleep 3
fi

# 3. Stop any previous QEMU instance and launch full-screen inside XFCE
pkill -x 'qemu-system-i386' >/dev/null 2>&1 || true
sleep 0.5

exec bash "$ROOT/tools/run-x11.sh" "$@"
