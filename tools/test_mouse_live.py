#!/usr/bin/env python3
"""Test real QEMU mouse movement and click on both x86_64 and aarch64."""
import os, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def test_arch(arch):
    out_dir = os.path.join(ROOT, "build-presets", f"desktop-{arch}")
    ser_log = os.path.join(out_dir, "mouse_test_serial.log")
    if os.path.exists(ser_log):
        os.remove(ser_log)
    if arch == "x86_64":
        cmd = [
            "qemu-system-x86_64",
            "-kernel", os.path.join(out_dir, "flairos-desktop-x86_64.mb.elf"),
            "-vga", "std", "-m", "256M",
            "-serial", f"file:{ser_log}",
            "-display", "none", "-monitor", "stdio", "-no-reboot",
        ]
    else:
        cmd = [
            "qemu-system-aarch64",
            "-M", "virt", "-cpu", "cortex-a57", "-m", "256M",
            "-device", "ramfb",
            "-device", "virtio-tablet-device", "-device", "virtio-keyboard-device",
            "-kernel", os.path.join(out_dir, "flairos-desktop-aarch64.elf"),
            "-serial", f"file:{ser_log}",
            "-display", "none", "-monitor", "stdio", "-no-reboot",
        ]
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    deadline = time.time() + 6.0
    while time.time() < deadline:
        if os.path.exists(ser_log) and "render=ok" in open(ser_log, errors="ignore").read():
            break
        time.sleep(0.1)

    for mx, my in [(200, 150), (450, 720)]:
        proc.stdin.write(f"mouse_move {mx} {my}\n")
        proc.stdin.flush()
        time.sleep(0.25)
    proc.stdin.write("mouse_button 1\n")
    proc.stdin.flush()
    time.sleep(0.2)
    proc.stdin.write("mouse_button 0\n")
    proc.stdin.flush()
    time.sleep(0.2)
    proc.stdin.write("quit\n")
    proc.stdin.flush()
    proc.wait(timeout=4)

    ser = open(ser_log, errors="ignore").read() if os.path.exists(ser_log) else ""
    print(f"=== {arch} ===")
    print(ser.strip())
    return "flairos64: mouse " in ser

if __name__ == "__main__":
    ok_x = test_arch("x86_64")
    ok_a = test_arch("aarch64")
    print(f"RESULT: x86_64={ok_x}, aarch64={ok_a}")
    sys.exit(0 if (ok_x and ok_a) else 1)
