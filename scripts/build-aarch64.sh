#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

for tool in cmake ninja aarch64-linux-gnu-gcc; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "Missing required tool: $tool" >&2
    echo "Run ./scripts/setup-aarch64.sh on Debian/Ubuntu, or install the AArch64 cross-toolchain." >&2
    exit 1
  fi
done

cmake --preset aarch64
cmake --build --preset aarch64 --parallel

OUTPUT="$ROOT/build-cmake-aarch64/aarch64/kernel.elf"
if [[ ! -s "$OUTPUT" ]]; then
  echo "Build completed but expected ELF was not created: $OUTPUT" >&2
  exit 1
fi

echo
echo "AArch64 kernel built: $OUTPUT"
if command -v qemu-system-aarch64 >/dev/null 2>&1; then
  echo "Run it with: cmake --build build-cmake-aarch64 --target aarch64_run"
else
  echo "QEMU is not installed; install qemu-system-arm to run the kernel."
fi
