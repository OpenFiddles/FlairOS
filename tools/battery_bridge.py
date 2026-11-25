#!/usr/bin/env python3
"""Stream Android Termux battery status into FlairOS serial bridge protocol:
   BAT <0-100> <0|1>
"""
import json, subprocess, sys, time

def read_bat():
    try:
        out = subprocess.check_output(["termux-battery-status"], stderr=subprocess.DEVNULL, timeout=3)
        d = json.loads(out.decode("utf-8"))
        pct = max(0, min(100, int(d.get("percentage", 100))))
        chg = 1 if str(d.get("status", "")).upper() in ("CHARGING", "FULL") else 0
        return f"BAT {pct} {chg}\n"
    except Exception:
        return "NOBAT\n"

if __name__ == "__main__":
    interval = float(sys.argv[1]) if len(sys.argv) > 1 else 5.0
    while True:
        sys.stdout.write(read_bat())
        sys.stdout.flush()
        time.sleep(interval)
