#!/data/data/com.termux/files/usr/bin/bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP_DIR="${PREFIX:-/tmp}/tmp"
mkdir -p "$TMP_DIR"

PID_FILE="$TMP_DIR/qemu-vnc.pid"
LOG_FILE="$TMP_DIR/qemu-vnc.log"
DISPLAY_SPEC="${VNC_DISPLAY:-0.0.0.0:0}"
CMD="${1:-start}"

case "$CMD" in
  start)
    if [ -f "$PID_FILE" ] && kill -0 "$(cat "$PID_FILE")" 2>/dev/null; then
      echo "QEMU VNC already running (PID $(cat "$PID_FILE")) on $DISPLAY_SPEC (port 5900)"
      exit 0
    fi
    cd "$ROOT"
    make -j4 legacy >/dev/null
    nohup qemu-system-i386 \
      -fda build/textile.img \
      -hda build/storage.img \
      -vga std -global VGA.vgamem_mb=64 \
      -vnc "$DISPLAY_SPEC" \
      -k en-us \
      -serial "file:$LOG_FILE" \
      -nic user,model=rtl8139 \
      -no-reboot \
      -m 256M \
      >"$TMP_DIR/qemu-vnc.out" 2>&1 &
    echo $! > "$PID_FILE"
    sleep 1
    if kill -0 "$(cat "$PID_FILE")" 2>/dev/null; then
      echo "QEMU VNC started (PID $(cat "$PID_FILE")) -> connect VNC client to 127.0.0.1:5900 (display :0)"
    else
      echo "QEMU VNC failed to start; see $TMP_DIR/qemu-vnc.out"
      exit 1
    fi
    ;;
  stop)
    if [ -f "$PID_FILE" ] && kill -0 "$(cat "$PID_FILE")" 2>/dev/null; then
      kill "$(cat "$PID_FILE")"
      rm -f "$PID_FILE"
      echo "QEMU VNC stopped"
    else
      rm -f "$PID_FILE"
      echo "QEMU VNC is not running"
    fi
    ;;
  status)
    if [ -f "$PID_FILE" ] && kill -0 "$(cat "$PID_FILE")" 2>/dev/null; then
      echo "QEMU VNC running (PID $(cat "$PID_FILE")) on 127.0.0.1:5900"
    else
      echo "QEMU VNC is not running"
      exit 1
    fi
    ;;
  *)
    echo "Usage: $0 {start|stop|status}"
    exit 2
    ;;
esac
