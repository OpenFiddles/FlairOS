# Pancake and PCK

**Pancake** is FlairOS's resource and cache bundle manager. Its short command name is
`pck`. The archive extension is `.pck`.

Important distinction:

- `.airp` is the executable application image/container.
- `.pck` is a **data-only** resource/cache bundle. It never contains runnable apps
  and the kernel must never pass PCK contents to the executable loader.

## PCK v1 layout

All integers are little-endian.

| Field | Size | Meaning |
|---|---:|---|
| Magic | 4 | ASCII `PCK1` |
| Version | 2 | 1 |
| Entry count | 2 | Number of resources |
| Index size | 4 | Entry count × 52 |
| Data offset | 4 | 16 + index size |

Each 52-byte index record contains a 32-byte UTF-8 name field (maximum 31 bytes),
64-bit absolute data offset, 64-bit data length, and CRC-32. Resources are stored
uncompressed in the data section. Names must be unique and cannot escape their
bundle namespace.

The first Pancake host utility supports packing, listing, verifying checksums, and
extracting resources. Compression, remote repositories, dependency resolution,
and on-device cache eviction are intentionally not part of v1.

## Host usage

```sh
python3 tools/pancake.py pack ui.pck assets/logo.bin assets/theme.dat
python3 tools/pancake.py list ui.pck
python3 tools/pancake.py verify ui.pck
python3 tools/pancake.py extract ui.pck ./cache
```
