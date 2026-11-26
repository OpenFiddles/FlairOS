# AIRP package format

AIRP is FlairOS's application container. A package holds an executable image, a
JSON manifest, and typed resources. This document is the on-disk contract;
`include/airp.h` is the authoritative declaration and `tools/airpkg.py` is the
reference implementation.

Version 1 is a legacy flat format that the loader still accepts. Version 2 is
the current format and adds manifests, resources, integrity checks and
architecture tagging.

All integers are little-endian. All offsets are relative to the start of the
file. Nothing is alignment-dependent: every field is read at a fixed byte offset
so a compiler's struct padding can never change the format.

## Why a container instead of a bare image

A bare linked image cannot describe itself. It has no place to record which
architecture it targets, what it needs from the system, what its name is, or
which assets belong to it. Every one of those facts used to be either hardcoded
in the kernel or inferred from a filename.

The container separates two things that used to be the same file:

| Concept | Where it lives |
| --- | --- |
| The container | The whole `.airp` file: header, manifest, resource table, resources |
| The executable | One payload region inside the container, linked for `TX_APP_BASE` |

That separation is what makes a component package possible. FlairDesktop.airp is
a container with no executable at all: the session shell implementation lives in
the kernel, and the package carries the declarative session definition (theme,
launcher layout, strings). A container is not required to hold code.

## Version 2 header (64 bytes)

| Offset | Size | Field | Notes |
| --- | --- | --- | --- |
| 0 | 4 | `magic` | `0x50524941`, bytes on disk `"AIRP"` |
| 4 | 2 | `version` | 2 |
| 6 | 2 | `header_size` | 64 |
| 8 | 4 | `api_level` | Highest syscall API the package needs |
| 12 | 4 | `image_base` | Must be `TX_APP_BASE` (`0x40000`), or 0 for a component |
| 16 | 4 | `entry` | Absolute entry address inside the image, or 0 for a component |
| 20 | 4 | `image_size` | Executable payload length in bytes, or 0 for a component |
| 24 | 4 | `flags` | `AIRP_FLAG_*` bitmask |
| 28 | 4 | `manifest_off` | Offset of the JSON manifest |
| 32 | 4 | `manifest_len` | Manifest length in bytes |
| 36 | 4 | `resource_count` | Number of resource records, at most 64 |
| 40 | 4 | `resource_tab_off` | Offset of the resource table |
| 44 | 4 | `resource_tab_len` | `resource_count * 24` |
| 48 | 4 | `arch` | `AIRP_ARCH_*` |
| 52 | 4 | `header_crc` | CRC-32 of bytes 0..63 with this field zeroed |
| 56 | 4 | `payload_crc` | CRC-32 of every byte after the header, up to `file_size` |
| 60 | 4 | `file_size` | Total container size on disk |

`payload_crc` covers the executable, the manifest, the resource table and every
resource, so a package is verified as a whole before any field in it is trusted.
`header_crc` is checked first, because the other fields cannot be believed until
the header itself is intact.

### Flags

| Value | Name | Meaning |
| --- | --- | --- |
| `0x0001` | `AIRP_FLAG_HAS_ICON` | Package carries an icon resource |
| `0x0002` | `AIRP_FLAG_SYSTEM` | Installed by the OS image, not a user app |
| `0x0004` | `AIRP_FLAG_DESKTOP` | Session shell package |
| `0x0008` | `AIRP_FLAG_RESOURCES_ONLY` | Component package: no executable |

### Architectures

| Value | Name |
| --- | --- |
| `0x0000` | `i386` |
| `0x0001` | `x86_64` |
| `0x0002` | `aarch64` |
| `0xFFFF` | any |

`AIRP_ARCH_ANY` in a package means it runs anywhere. A loader asked for a
specific architecture accepts only that architecture or `ANY`; it rejects
anything else rather than attempting to load foreign code.

## Version 1 header (36 bytes, legacy)

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | `magic` |
| 4 | 2 | `version` (1) |
| 6 | 2 | `header_size` (36) |
| 8 | 4 | `api_level` |
| 12 | 4 | `image_base` |
| 16 | 4 | `entry` |
| 20 | 4 | `image_size` |
| 24 | 12 | `name`, NUL padded |

Version 1 has no manifest, no resources and no checksums. It is accepted for
compatibility and reported as version 1; a loader must not pretend it has
integrity data it does not carry.

## Resource table

Each record is exactly 24 bytes:

| Offset | Size | Field | Notes |
| --- | --- | --- | --- |
| 0 | 4 | `name_off` | Absolute offset of a NUL-terminated UTF-8 name |
| 4 | 4 | `data_off` | Absolute offset of the resource bytes |
| 8 | 4 | `data_len` | Resource length in bytes |
| 12 | 4 | `crc` | CRC-32 of the resource bytes |
| 16 | 2 | `type` | `AIRP_RT_*` |
| 18 | 2 | `flags` | `AIRP_RF_*` |
| 20 | 4 | `reserved` | Must be 0 |

Names live in a blob directly after the table. A `name_off` that points below
`resource_tab_off + resource_tab_len`, or past `file_size`, is invalid: without
that rule a crafted record could make a name alias the executable payload.

### Resource types

| Value | Name | Typical content |
| --- | --- | --- |
| 0 | `BLOB` | Unclassified binary data |
| 1 | `MANIFEST` | A second manifest copy |
| 2 | `THEME` | Theme definition (JSON) |
| 3 | `ICON` | Icon bitmap |
| 4 | `FONT` | Font data |
| 5 | `SOUND` | Audio |
| 6 | `STRINGS` | Localised strings (JSON) |
| 7 | `UI` | UI layout (JSON) |
| 8 | `PLUGIN` | Nested package |
| 9 | `LIBRARY` | Shared library |

### Resource flags

| Value | Name | Meaning |
| --- | --- | --- |
| `0x0001` | `AIRP_RF_REQUIRED` | Package is unusable without this resource |

## Integrity

CRC-32 is the IEEE 802.3 reflected variant, polynomial `0xEDB88320`, init and
final XOR both `0xFFFFFFFF`. The check value for `"123456789"` is `0xCBF43926`,
and both the kernel and the tooling assert it.

Three separate checks exist because they fail for different reasons:

- **Header CRC** catches a damaged or malformed header before any offset is used.
- **Payload CRC** catches any damage to the body, including resources.
- **Per-resource CRC** lets a loader verify a single resource it is about to use,
  and localises the failure to a named resource in diagnostics.

A packer must compute `payload_crc` over the *finished* body. The resource table
is reserved as zeros while offsets are being computed and filled afterwards, so
a checksum taken before that final write would be wrong.

## Loading rules

A loader validates in this order, because each step depends on the previous one:

1. File is at least a v1 header long.
2. Magic matches.
3. Version is 1 or 2; header size matches the version.
4. Header CRC matches (v2).
5. `api_level` is not newer than the loader supports.
6. `file_size` is not larger than the buffer. A boot staging buffer is
   zero-padded to whole sectors, so `file_size <= len` is correct and
   `file_size == len` is not.
7. Architecture matches, if the caller asked for one.
8. `image_base` is this kernel's app slot, unless the package is a component.
9. `image_size` is in range and fits inside the declared package.
10. `entry` lies inside the image.
11. Manifest and resource table are inside the declared package.
12. Table length matches the resource count.
13. Payload CRC matches.
14. Every resource is in range, its name is in the name blob, and its CRC
    matches.

Any failure returns a stable `AIRP_LOAD_*` code. A loader must never copy a
payload into the app slot before step 13, and never execute anything it has not
validated.

## Component packages

A package with `AIRP_FLAG_RESOURCES_ONLY` has `image_size == 0` and `entry == 0`
and carries no executable. It is valid, and it is how a declarative part of the
system ships: `FlairDesktop.airp` is one.

A loader must refuse a resources-only package that declares an executable, and
must refuse an ordinary package with no executable. The flag and the header
fields have to agree; treating either mismatch as "probably fine" is how a
malformed package becomes a security problem.

## Command-line tool

```
tools/airpkg.py pack     IMAGE | -  OUT.airp [--name N] [--manifest FILE]
                         [--resource NAME=PATH[:TYPE] ...] [--arch A]
                         [--flags icon,system,desktop,resources-only]
tools/airpkg.py list     PACKAGE.airp
tools/airpkg.py info     PACKAGE.airp
tools/airpkg.py validate PACKAGE.airp [--arch A]
tools/airpkg.py extract  PACKAGE.airp DIRECTORY
tools/airpkg.py manifest PACKAGE.airp
```

`pack` with `-` as the input and the `resources-only` flag builds a component
package.

Example, the desktop session package exactly as the build produces it:

```
tools/airpkg.py pack - build/FlairDesktop.airp \
  --name FlairDesktop --flags desktop,system,resources-only --arch i386 \
  --manifest desktop/manifest/desktop.json \
  --resource 'theme.json=desktop/themes/default.json:theme' \
  --resource 'launcher.json=desktop/resources/launcher.json:ui' \
  --resource 'strings.json=desktop/resources/strings.json:strings'
```

`validate` is independent of the kernel loader, and `tests/airp_pkg_test.py`
re-parses every header field, checksum and resource byte by hand so a bug in
the packer cannot hide behind its own validator.

## Version history

| Version | Change |
| --- | --- |
| 1 | 36-byte header plus flat image. No manifest, resources or checksums. |
| 2 | 64-byte header, flags, manifest, typed resource table, CRC-32 integrity, architecture tag, component packages. Version 1 remains readable. |
