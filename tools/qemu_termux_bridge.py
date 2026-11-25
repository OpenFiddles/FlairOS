#!/usr/bin/env python3
"""QEMU <-> Termux Host Bridge for FlairOS (Telemetry v2 + HTTP Proxy + Fixture Replay)."""
import argparse
import glob
import json
import os
import re
import socketserver
import subprocess
import time
import urllib.error
import urllib.request

HOST = "127.0.0.1"
PORT = int(os.environ.get("FLAIROS_BRIDGE_PORT", "5910"))

_cache_ts = 0.0
_cache_v2 = {
    "battery_pct": 100,
    "ac_power": 1,
    "battery_temp_c10": 250,
    "wifi_up": 1,
    "wifi_rssi": -55,
    "wifi_bars": 4,
    "wifi_ssid": "FlairNet",
    "wifi_ip": "10.0.2.15",
    "cell_up": 1,
    "cell_bars": 4,
    "cell_roaming": 0,
    "cell_carrier": "FlairTel",
    "cell_rat": "5G",
    "bt_available": 1,
    "bt_powered": 0,
    "bt_connected": 0,
    "bt_name": "None",
    "source": "termux-bridge",
}
_fixture_path = None


def _run_quick(cmd, timeout=0.45):
    try:
        res = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        return res.stdout.strip() if res.returncode == 0 else ""
    except Exception:
        return ""


def _sanitize(val, max_len, fallback):
    s = str(val or "").replace("|", "/").replace("\n", " ").replace("\r", " ").strip()
    return (s[:max_len] if s else fallback)


def _rssi_to_bars(rssi):
    if rssi >= -55:
        return 4
    if rssi >= -67:
        return 3
    if rssi >= -78:
        return 2
    if rssi >= -88:
        return 1
    return 0


def read_termux_telemetry_v2():
    global _cache_ts, _cache_v2
    if _fixture_path:
        with open(_fixture_path, "r", encoding="utf-8") as f:
            data = json.load(f)
        merged = dict(_cache_v2)
        merged.update(data)
        merged["source"] = _sanitize(merged.get("source", "fixture"), 23, "fixture")
        return merged

    now = time.time()
    if now - _cache_ts < 5.0:
        return _cache_v2

    d = dict(_cache_v2)
    # 1. Battery from /sys/class/power_supply (zero wake-lock)
    for cap_path in glob.glob("/sys/class/power_supply/*/capacity"):
        try:
            d["battery_pct"] = max(0, min(100, int(open(cap_path).read().strip())))
            base = os.path.dirname(cap_path)
            st_path = os.path.join(base, "status")
            if os.path.exists(st_path):
                st = open(st_path).read().strip().upper()
                d["ac_power"] = 1 if st in ("CHARGING", "FULL") else 0
            tp_path = os.path.join(base, "temp")
            if os.path.exists(tp_path):
                d["battery_temp_c10"] = int(open(tp_path).read().strip())
            break
        except Exception:
            pass

    # 2. Wi-Fi operstate + IP + optional SSID/RSSI
    wifi = 0
    iface_name = ""
    for oper in glob.glob("/sys/class/net/wlan*/operstate"):
        try:
            if open(oper).read().strip().lower() == "up":
                wifi = 1
                iface_name = os.path.basename(os.path.dirname(oper))
                break
        except Exception:
            pass
    if wifi == 0 and os.path.exists("/proc/net/route"):
        try:
            lines = open("/proc/net/route").read().splitlines()[1:]
            for ln in lines:
                cols = ln.split()
                if len(cols) > 1 and cols[1] == "00000000":
                    wifi = 1
                    iface_name = cols[0]
                    break
        except Exception:
            pass
    d["wifi_up"] = wifi
    d["wifi_bars"] = 4 if wifi else 0
    if wifi and iface_name:
        ip_out = _run_quick(["ip", "-4", "-o", "addr", "show", iface_name])
        m_ip = re.search(r"inet\s+(\d+\.\d+\.\d+\.\d+)", ip_out)
        if m_ip:
            d["wifi_ip"] = m_ip.group(1)

    # 3. Cellular operator & RAT via Android getprop (instant, no wake-lock)
    op = _run_quick(["getprop", "gsm.operator.alpha"])
    if op:
        d["cell_carrier"] = _sanitize(op.split(",")[0], 23, "FlairTel")
        d["cell_up"] = 1
    nt = _run_quick(["getprop", "gsm.network.type"])
    if nt:
        nt_first = nt.split(",")[0].strip().upper()
        if "NR" in nt_first or "5G" in nt_first:
            d["cell_rat"] = "5G"
        elif "LTE" in nt_first:
            d["cell_rat"] = "LTE"
        elif nt_first and nt_first != "UNKNOWN":
            d["cell_rat"] = _sanitize(nt_first, 11, "4G")
    roam = _run_quick(["getprop", "gsm.operator.isroaming"])
    if roam:
        d["cell_roaming"] = 1 if "true" in roam.lower() else 0

    # 4. Bluetooth state via settings (instant)
    bt_on = _run_quick(["settings", "get", "global", "bluetooth_on"])
    if bt_on in ("0", "1"):
        d["bt_available"] = 1
        d["bt_powered"] = int(bt_on)

    if d["wifi_up"] and d["wifi_rssi"] < 0:
        d["wifi_bars"] = _rssi_to_bars(d["wifi_rssi"])
    d["source"] = "termux-bridge"

    _cache_ts = now
    _cache_v2 = d
    return _cache_v2


def format_stat2(d):
    nums = (
        f"STAT2 {int(d['battery_pct'])} {int(d['ac_power'])} {int(d['battery_temp_c10'])} "
        f"{int(d['wifi_up'])} {int(d['wifi_rssi'])} {int(d['wifi_bars'])} "
        f"{int(d['cell_up'])} {int(d['cell_bars'])} {int(d['cell_roaming'])} "
        f"{int(d['bt_available'])} {int(d['bt_powered'])} {int(d['bt_connected'])}"
    )
    strs = "|".join([
        _sanitize(d.get("wifi_ssid"), 31, "FlairNet"),
        _sanitize(d.get("wifi_ip"), 23, "0.0.0.0"),
        _sanitize(d.get("cell_carrier"), 23, "FlairTel"),
        _sanitize(d.get("cell_rat"), 11, "5G"),
        _sanitize(d.get("bt_name"), 23, "None"),
        _sanitize(d.get("source"), 23, "termux-bridge"),
    ])
    return f"{nums}|{strs}"


def fetch_http(url):
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "FlairBrowser/1.0"})
        with urllib.request.urlopen(req, timeout=4.0) as resp:
            status = getattr(resp, "status", 200)
            body = resp.read(4096).decode("utf-8", errors="replace")
            return status, body
    except urllib.error.HTTPError as e:
        return e.code, str(e)
    except Exception as e:
        return 502, f"<html><body><h1>Network Error</h1><p>{e}</p></body></html>"


class BridgeHandler(socketserver.StreamRequestHandler):
    def handle(self):
        while True:
            raw = self.rfile.readline(1024)
            if not raw:
                break
            line = raw.decode("utf-8", errors="replace").strip()
            if not line:
                continue
            if line == "PING":
                self.wfile.write(b"PONG\n")
            elif line == "STAT":
                d = read_termux_telemetry_v2()
                self.wfile.write(
                    f"STAT {d['battery_pct']} {d['ac_power']} {d['wifi_up']}\n".encode("ascii")
                )
            elif line == "STAT2":
                d = read_termux_telemetry_v2()
                self.wfile.write((format_stat2(d) + "\n").encode("utf-8"))
            elif line.startswith("GET "):
                url = line[4:].strip()
                status, body = fetch_http(url)
                body = body.replace("\r", " ")
                header = f"HTTP {status} {len(body)}\n".encode("ascii")
                self.wfile.write(header + body.encode("utf-8") + b"\nEND_HTTP\n")
            else:
                self.wfile.write(b"ERR unknown\n")
            self.wfile.flush()


class ReusableServer(socketserver.ThreadingMixIn, socketserver.TCPServer):
    allow_reuse_address = True
    daemon_threads = True


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description="FlairOS QEMU Termux Bridge")
    ap.add_argument("--fixture", help="Path to JSON telemetry fixture for deterministic replay")
    ap.add_argument("--once", action="store_true", help="Print one STAT2 line and exit")
    args = ap.parse_args()
    _fixture_path = args.fixture
    if args.once:
        print(format_stat2(read_termux_telemetry_v2()))
    else:
        with ReusableServer((HOST, PORT), BridgeHandler) as srv:
            print(f"FlairOS QEMU Termux Bridge listening on {HOST}:{PORT}", flush=True)
            srv.serve_forever()
