#!/usr/bin/env python3
"""Drive FlairOS in headless QEMU over QMP: absolute mouse, keys, screenshots, serial checks.

Examples:
  python3 tools/qemu_ui.py --out docs/screenshots \
      --steps "wait:22,shot:desktop,move:0.06,0.10,shot:hover,click,wait:2,shot:window" \
      --expect "io: selftest passed" --expect "pmm: selftest passed"

Steps (comma separated):
  wait:SECONDS          sleep
  shot:NAME             save OUT/NAME.png
  move:X,Y              absolute pointer, X and Y in 0.0 .. 1.0 of the screen
  down | up | click     left button (click = down, short pause, up)
  rclick                right click
  key:QCODE             press a key (tab, ret, esc, a, 3, ...)
  type:TEXT             type letters, digits and spaces
The run fails (exit 1) if the serial log contains PANIC or a --expect text is missing.
"""
import argparse, json, os, shutil, socket, subprocess, sys, tempfile, time

def qmp_connect(path, timeout=30):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            s = socket.socket(socket.AF_UNIX)
            s.connect(path)
            f = s.makefile("rw")
            json.loads(f.readline())            # greeting
            f.write(json.dumps({"execute": "qmp_capabilities"}) + "\n"); f.flush()
            json.loads(f.readline())
            return s, f
        except (FileNotFoundError, ConnectionRefusedError, OSError):
            time.sleep(0.3)
    raise RuntimeError("could not connect to QMP socket")

def qmp(f, cmd, **args):
    f.write(json.dumps({"execute": cmd, "arguments": args}) + "\n"); f.flush()
    while True:
        line = f.readline()
        if not line:
            raise RuntimeError("QMP closed")
        msg = json.loads(line)
        if "return" in msg:
            return msg["return"]
        if "error" in msg:
            raise RuntimeError(f"{cmd}: {msg['error']['desc']}")

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--image", default="build/textile.img")
    ap.add_argument("--out", default="build/shots")
    ap.add_argument("--steps", default="wait:22,shot:desktop")
    ap.add_argument("--expect", action="append", default=[])
    ap.add_argument("--serial", default=None, help="serial log path (default: temp)")
    ap.add_argument("--qemu", default="qemu-system-i386")
    ap.add_argument("--extra", default="", help="extra QEMU arguments")
    a = ap.parse_args()

    os.makedirs(a.out, exist_ok=True)
    tmp = tempfile.mkdtemp(prefix="flairos-ui-")
    fda = os.path.join(tmp, "fda.img"); shutil.copy(a.image, fda)   # private copy: no lock clashes
    hda = os.path.join(tmp, "hda.img")
    with open(hda, "wb") as h: h.truncate(30 * 1024 * 1024)
    serial = a.serial or os.path.join(tmp, "serial.log")
    sock = os.path.join(tmp, "qmp.sock")
    cmd = [a.qemu, "-drive", f"file={fda},format=raw,if=floppy", "-drive", f"file={hda},format=raw,if=ide",
           "-display", "none", "-serial", f"file:{serial}", "-qmp", f"unix:{sock},server=on,wait=off",
           "-no-reboot", "-m", "256M"] + a.extra.split()
    proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    ok = True
    try:
        s, f = qmp_connect(sock)
        for step in split_steps(a.steps):
            run_step(f, step, a.out)
        qmp(f, "quit")
    except Exception as e:
        print("ERROR:", e); ok = False
    finally:
        try: proc.wait(timeout=10)
        except Exception: proc.kill()
    log = open(serial, errors="replace").read() if os.path.exists(serial) else ""
    if "PANIC" in log:
        print("FAIL: PANIC in serial log"); ok = False
    for text in a.expect:
        if text not in log:
            print(f"FAIL: serial log lacks: {text}"); ok = False
        else:
            print(f"ok: {text}")
    print("serial log:", serial)
    sys.exit(0 if ok else 1)

def split_steps(spec):
    """Split on commas, but keep 'move:X,Y' together."""
    out, parts = [], spec.split(",")
    i = 0
    while i < len(parts):
        if parts[i].startswith("move:") and i + 1 < len(parts):
            out.append(parts[i] + "," + parts[i + 1]); i += 2
        else:
            out.append(parts[i]); i += 1
    return out

def btn(f, button, down):
    qmp(f, "input-send-event", events=[{"type": "btn", "data": {"down": down, "button": button}}])

def run_step(f, step, out):
    name, _, arg = step.partition(":")
    if name == "wait":
        time.sleep(float(arg))
    elif name == "shot":
        path = os.path.abspath(os.path.join(out, arg + ".png"))
        qmp(f, "screendump", filename=path, format="png")
        print("shot:", path)
    elif name == "move":
        x, y = (float(v) for v in arg.split(","))
        qmp(f, "input-send-event", events=[
            {"type": "abs", "data": {"axis": "x", "value": int(x * 32767)}},
            {"type": "abs", "data": {"axis": "y", "value": int(y * 32767)}}])
        time.sleep(0.8)
    elif name == "down": btn(f, "left", True)
    elif name == "up": btn(f, "left", False)
    elif name == "click":
        btn(f, "left", True); time.sleep(0.15); btn(f, "left", False); time.sleep(0.5)
    elif name == "rclick":
        btn(f, "right", True); time.sleep(0.15); btn(f, "right", False); time.sleep(0.5)
    elif name == "key":
        qmp(f, "send-key", keys=[{"type": "qcode", "data": arg}]); time.sleep(0.6)
    elif name == "type":
        for ch in arg:
            q = "spc" if ch == " " else ch
            qmp(f, "send-key", keys=[{"type": "qcode", "data": q}]); time.sleep(0.4)
    else:
        raise RuntimeError(f"unknown step: {step}")

if __name__ == "__main__":
    main()
