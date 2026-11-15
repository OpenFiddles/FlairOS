#!/usr/bin/env bash
set -euo pipefail

if ! command -v apt-get >/dev/null 2>&1; then
  echo "This helper supports Debian/Ubuntu systems with apt-get." >&2
  echo "Install: cmake ninja-build gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu qemu-system-arm gdb-multiarch" >&2
  exit 1
fi

if [[ "$(id -u)" -eq 0 ]]; then
  SUDO=()
else
  SUDO=(sudo)
fi

"${SUDO[@]}" apt-get update
"${SUDO[@]}" apt-get install -y \
  build-essential cmake ninja-build \
  gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu \
  qemu-system-arm gdb-multiarch

echo
echo "AArch64 build environment is installed."
echo "Next: ./scripts/build-aarch64.sh"
