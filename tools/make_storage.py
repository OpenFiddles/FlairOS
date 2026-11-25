#!/usr/bin/env python3
"""Create a dynamic sparse storage.img safely bounded by host free disk space."""
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "storage.img")
os.makedirs(os.path.dirname(os.path.abspath(OUT)), exist_ok=True)

TARGET_MAX = 2 * 1024 * 1024 * 1024   # 2 GiB cap
RESERVE_FREE = 2 * 1024 * 1024 * 1024  # Keep >= 2 GiB free for Android/Termux
MIN_SIZE = 64 * 1024 * 1024            # 64 MiB floor

st = os.statvfs(os.path.dirname(os.path.abspath(OUT)))
free_bytes = st.f_bavail * st.f_frsize
safe_cap = max(MIN_SIZE, free_bytes - RESERVE_FREE)
chosen = min(TARGET_MAX, safe_cap)
# Align down to 1 MiB boundary
chosen = (chosen // (1024 * 1024)) * (1024 * 1024)

with open(OUT, "wb") as f:
    f.truncate(chosen)

gb = chosen / (1024 ** 3)
free_gb = free_bytes / (1024 ** 3)
print(f"[OK] Dynamic sparse storage: {OUT} -> {gb:.2f} GiB (host free: {free_gb:.2f} GiB, reserved: 2.00 GiB)")
