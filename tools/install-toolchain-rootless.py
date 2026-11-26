#!/usr/bin/env python3
"""Install the FlairOS 64-bit toolchain without root.

For environments where apt-get cannot run (containers with no_new_privs, CI
sandboxes, Termux-like prefixes), this resolves the Ubuntu packages the preset
build needs, downloads them, and extracts them into a private prefix with
`dpkg-deb -x`. Nothing is written outside the prefix.

Packages: clang-18, lld-18, llvm-18 (llvm-objcopy), qemu-system-x86,
qemu-system-arm, libfuse3-3 (QEMU links it; Ubuntu ships it in /lib, not
/usr/lib, so it needs its own entry in LD_LIBRARY_PATH).

Usage:
    python3 tools/install-toolchain-rootless.py [--prefix DIR] [--suite noble]

Afterwards, source the printed exports (or use ./flairos-start.sh, which sets
them up automatically).
"""
import argparse
import gzip
import os
import re
import subprocess
import sys
import urllib.request

ARCHIVE = "http://archive.ubuntu.com/ubuntu"
COMPONENTS = ("main", "universe")
ROOTS = ["clang-18", "lld-18", "llvm-18", "qemu-system-x86", "qemu-system-arm", "libfuse3-3", "nasm"]
BINARIES = [
    ("clang", ("usr/bin/clang-18", "usr/lib/llvm-18/bin/clang")),
    ("ld.lld", ("usr/bin/ld.lld-18", "usr/lib/llvm-18/bin/ld.lld")),
    ("llvm-objcopy", ("usr/bin/llvm-objcopy-18", "usr/lib/llvm-18/bin/llvm-objcopy")),
    ("nasm", ("usr/bin/nasm",)),
    ("xorriso", ("usr/bin/xorriso",)),
    ("qemu-system-x86_64", ("usr/bin/qemu-system-x86_64",)),
    ("qemu-system-aarch64", ("usr/bin/qemu-system-aarch64",)),
]
BIOS_FILES = ("bios-256k.bin", "bios.bin", "vgabios-stdvga.bin", "vgabios.bin")


def log(msg):
    print(msg, flush=True)


def index_paths(dl, suite):
    return [os.path.join(dl, f"Packages-{c}.gz") for c in COMPONENTS], \
           [f"{ARCHIVE}/dists/{suite}/{c}/binary-amd64/Packages.gz" for c in COMPONENTS]


def fetch_index(dl, suite):
    os.makedirs(dl, exist_ok=True)
    local, urls = index_paths(dl, suite)
    for path, url in zip(local, urls):
        if not os.path.exists(path) or os.path.getsize(path) < 1024:
            log(f"[info] index: {url}")
            urllib.request.urlretrieve(url, path)
    return local


def load_index(paths):
    pkgs = {}
    for path in paths:
        cur, buf = None, {}
        with gzip.open(path, "rt", errors="replace") as fh:
            for line in fh:
                if line.startswith("Package: "):
                    cur, buf = line.split(": ", 1)[1].strip(), {}
                elif line.startswith("Filename: "):
                    buf["file"] = line.split(": ", 1)[1].strip()
                elif line.startswith("Depends: "):
                    buf["depends"] = line.split(": ", 1)[1].strip()
                elif line.startswith("Size: "):
                    try:
                        buf["size"] = int(line.split(": ", 1)[1].strip())
                    except ValueError:
                        pass
                elif not line.strip():
                    if cur and "file" in buf:
                        pkgs.setdefault(cur, buf)
                    cur, buf = None, {}
        if cur and "file" in buf:
            pkgs.setdefault(cur, buf)
    return pkgs


_status = {}
_no_host_check = False


def host_has(name):
    """True when the package is already installed on the host.

    Some minimal images register packages in dpkg without shipping their files,
    so --no-host-check forces extraction of the whole dependency tree.
    """
    if _no_host_check:
        return False
    if name not in _status:
        res = subprocess.run(["dpkg-query", "-W", "-f=${Status}", name],
                             capture_output=True, text=True)
        _status[name] = res.returncode == 0 and "install ok installed" in res.stdout
    return _status[name]


def resolve(roots, pkgs):
    need, seen, queue = {}, set(), list(roots)
    while queue:
        name = queue.pop()
        if name in seen:
            continue
        seen.add(name)
        if name not in pkgs:
            log(f"[warn] not in index, skipping: {name}")
            continue
        if host_has(name):
            continue
        need[name] = pkgs[name]
        for dep in pkgs[name].get("depends", "").split(","):
            dep = dep.strip()
            if not dep:
                continue
            first = re.sub(r"\(.*?\)", "", dep.split("|")[0]).strip().split(":")[0].strip()
            if first and first not in seen:
                queue.append(first)
    return need


def install(need, dl, prefix):
    os.makedirs(prefix, exist_ok=True)
    for i, (name, meta) in enumerate(sorted(need.items()), 1):
        deb = os.path.join(dl, os.path.basename(meta["file"]))
        want = meta.get("size", -1)
        if not (os.path.exists(deb) and want > 0 and os.path.getsize(deb) == want):
            urllib.request.urlretrieve(f"{ARCHIVE}/{meta['file']}", deb)
        subprocess.run(["dpkg-deb", "-x", deb, prefix], check=True)
        log(f"[{i}/{len(need)}] {name}")


def link_binaries(prefix):
    bindir = os.path.join(prefix, "bin")
    os.makedirs(bindir, exist_ok=True)
    missing = []
    for name, candidates in BINARIES:
        for rel in candidates:
            src = os.path.join(prefix, rel)
            if os.access(src, os.X_OK):
                dst = os.path.join(bindir, name)
                if os.path.islink(dst) or os.path.exists(dst):
                    os.unlink(dst)
                os.symlink(src, dst)
                break
        else:
            missing.append(name)
    return missing


def link_bios(prefix):
    """Gather BIOS and option ROMs into one dir; QEMU is pointed at it with -L.

    Ubuntu scatters these: seabios keeps the BIOS images in /usr/share/seabios
    and ipxe-qemu keeps the NIC option ROMs in /usr/lib/ipxe[ /qemu]. QEMU looks
    in a single hardcoded directory, so without this it fails with
    'failed to find romfile "efi-e1000.rom"'.
    """
    qemu_dir = os.path.join(prefix, "usr/share/qemu")
    os.makedirs(qemu_dir, exist_ok=True)

    seabios_dir = os.path.join(prefix, "usr/share/seabios")
    for fname in BIOS_FILES:
        src = os.path.join(seabios_dir, fname)
        dst = os.path.join(qemu_dir, fname)
        if os.path.exists(src) and not os.path.exists(dst):
            os.symlink(src, dst)

    for rom_dir in ("usr/lib/ipxe/qemu", "usr/lib/ipxe"):
        full = os.path.join(prefix, rom_dir)
        if not os.path.isdir(full):
            continue
        for fname in sorted(os.listdir(full)):
            if not fname.endswith(".rom"):
                continue
            dst = os.path.join(qemu_dir, fname)
            if not os.path.exists(dst):
                os.symlink(os.path.join(full, fname), dst)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--prefix", default=os.path.expanduser("~/local"))
    ap.add_argument("--download-dir", default=os.path.expanduser("~/dl"))
    ap.add_argument("--suite", default="noble")
    ap.add_argument("--packages", nargs="+", default=ROOTS)
    ap.add_argument("--no-host-check", action="store_true",
                    help="extract packages even when dpkg reports them installed")
    args = ap.parse_args()

    global _no_host_check
    _no_host_check = args.no_host_check

    prefix = os.path.abspath(args.prefix)
    dl = os.path.abspath(args.download_dir)

    paths = fetch_index(dl, args.suite)
    pkgs = load_index(paths)
    if not pkgs:
        log("[fail] package index is empty; wrong --suite?")
        return 1

    need = resolve(args.packages, pkgs)
    log(f"[info] {len(need)} packages to install")
    install(need, dl, prefix)

    link_bios(prefix)
    missing = link_binaries(prefix)
    if missing:
        log(f"[fail] could not provide: {', '.join(missing)}")
        return 1

    lib = os.path.join(prefix, "lib/x86_64-linux-gnu")
    ulib = os.path.join(prefix, "usr/lib/x86_64-linux-gnu")
    log(f"""
[ok] toolchain installed into {prefix}

  export PATH="{prefix}/bin:$PATH"
  export LD_LIBRARY_PATH="{lib}:{ulib}"

QEMU firmware lives in {prefix}/usr/share/qemu; pass
  -L {prefix}/usr/share/qemu
so it finds the BIOS images.""")
    return 0


if __name__ == "__main__":
    sys.exit(main())
