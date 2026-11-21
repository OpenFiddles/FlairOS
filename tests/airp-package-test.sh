#!/bin/sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT HUP INT TERM
mkdir -p "$TMP/package/rootfs/usr/share/airp-test" "$TMP/root"
printf 'name=airp-test\nversion=1.0\n' > "$TMP/package/manifest"
printf 'installed by AirP\n' > "$TMP/package/rootfs/usr/share/airp-test/marker"
tar -czf "$TMP/airp-test.airpkg" -C "$TMP/package" manifest rootfs

AIRP_ROOT="$TMP/root" sh "$ROOT/system/bin/airp" install "$TMP/airp-test.airpkg"
test -s "$TMP/root/usr/share/airp-test/marker"
AIRP_ROOT="$TMP/root" sh "$ROOT/system/bin/airp" info airp-test | grep -q 'version=1.0'
AIRP_ROOT="$TMP/root" sh "$ROOT/system/bin/airp" remove airp-test
test ! -e "$TMP/root/usr/share/airp-test/marker"

printf 'AIRP application container\n' > "$TMP/sample.airp"
AIRP_ROOT="$TMP/root" sh "$ROOT/system/bin/airp" install "$TMP/sample.airp"
test -s "$TMP/root/opt/flairos/apps/sample.airp"
AIRP_ROOT="$TMP/root" sh "$ROOT/system/bin/airp" remove sample
test ! -e "$TMP/root/opt/flairos/apps/sample.airp"

echo "AirP package manager tests passed."
