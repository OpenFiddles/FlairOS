#!/bin/sh
# Boots the system image headless in QEMU and checks the requirement evidence
# listed in docs/REQUIREMENTS.md. Fails if a line is missing or the log has PANIC.
# Needs: qemu-system-i386, python3, and a built build/textile.img (make all).
set -eu
cd "$(dirname "$0")/.."
[ -f build/textile.img ] || { echo "build/textile.img missing: run 'make all' first"; exit 2; }

exec python3 tools/qemu_ui.py \
  --out build/shots \
  --steps "wait:${BOOT_WAIT:-26},shot:acceptance_boot" \
  --expect "hal: i386" \
  --expect "pmm: selftest passed" \
  --expect "vm: paging on" \
  --expect "vm: selftest passed" \
  --expect "kheap: selftest passed" \
  --expect "io: selftest passed" \
  --expect "ata: primary disk ready" \
  --expect "mouse: vmmouse absolute pointer" \
  --expect "display: AirGraph pixel desktop active" \
  --expect "desktop: start"
