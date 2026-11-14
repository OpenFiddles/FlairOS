#!/usr/bin/env python3
"""Fetch the exact upstream MicroPython tag used by the FlareOS port."""
import pathlib
import shutil
import subprocess
import sys

REPOSITORY = "https://github.com/micropython/micropython.git"


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: fetch_micropython.py DESTINATION TAG")
    destination = pathlib.Path(sys.argv[1]).resolve()
    tag = sys.argv[2]
    if not tag.startswith("v"):
        raise SystemExit("use an explicit MicroPython vX.Y.Z tag")

    if destination.exists():
        try:
            current = subprocess.run(
                ["git", "-C", str(destination), "describe", "--tags", "--exact-match"],
                check=True, capture_output=True, text=True,
            ).stdout.strip()
            if current == tag:
                print(f"MicroPython {tag} already present at {destination}")
                return
        except (subprocess.CalledProcessError, OSError):
            pass
        shutil.rmtree(destination)

    destination.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        ["git", "clone", "--depth", "1", "--single-branch", "--branch", tag,
         REPOSITORY, str(destination)],
        check=True, text=True,
    )
    actual = subprocess.run(
        ["git", "-C", str(destination), "describe", "--tags", "--exact-match"],
        check=True, capture_output=True, text=True,
    ).stdout.strip()
    if actual != tag:
        raise SystemExit(f"expected {tag}, got {actual}")
    print(f"Fetched MicroPython {tag} into {destination}")


if __name__ == "__main__":
    main()
