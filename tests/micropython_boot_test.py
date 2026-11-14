#!/usr/bin/env python3
"""Boot the Python-enabled disk, import flareos, and exit back to the desktop."""
import os
import socket
import subprocess
import sys
import tempfile
import time

image = sys.argv[1] if len(sys.argv) > 1 else "build/textile-python.img"
qemu = os.environ.get("QEMU", "qemu-system-i386")
work = tempfile.mkdtemp(prefix="flareos-python-")
monitor_path = os.path.join(work, "monitor.sock")
serial_path = os.path.join(work, "serial.log")
proc = subprocess.Popen([
    qemu, "-drive", "file=" + image + ",format=raw,if=floppy",
    "-display", "none", "-no-reboot",
    "-serial", "file:" + serial_path,
    "-monitor", "unix:" + monitor_path + ",server,nowait",
])
try:
    for _ in range(150):
        if os.path.exists(monitor_path):
            break
        if proc.poll() is not None:
            raise RuntimeError("QEMU exited before opening its monitor")
        time.sleep(0.1)
    monitor = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    monitor.connect(monitor_path)

    def command(value, delay=0.2):
        monitor.sendall((value + "\n").encode())
        time.sleep(delay)

    time.sleep(7)
    command("sendkey tab", 0.5)
    command("sendkey 5", 1.0)  # Start-menu Python entry.

    # Import the built-in OS module from the friendly REPL.
    for key in ["i", "m", "p", "o", "r", "t", "spc",
                "f", "l", "a", "r", "e", "o", "s"]:
        command("sendkey " + key, 0.12)
    command("sendkey ret", 1.0)

    # The module's exit hook returns through SYS_EXIT to the desktop.
    for key in ["f", "l", "a", "r", "e", "o", "s", "dot",
                "e", "x", "i", "t", "shift-9", "shift-0"]:
        command("sendkey " + key, 0.12)
    command("sendkey ret", 1.5)
    time.sleep(1.0)
finally:
    proc.kill()
    try:
        monitor.close()
    except Exception:
        pass

try:
    serial = open(serial_path, errors="ignore").read()
except OSError:
    serial = ""
print(serial[-2500:])
checks = [
    ("Python package staged", "python: package staged" in serial),
    ("Python launcher entered", "python: launching MicroPython" in serial),
    ("REPL returned to desktop", "python: returned to desktop" in serial),
    ("no kernel panic", "PANIC" not in serial.upper()),
]
for name, passed in checks:
    print(("PASS " if passed else "FAIL ") + name)
if not all(passed for _, passed in checks):
    sys.exit(1)
