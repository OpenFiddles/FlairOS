# AirP (AirPacket)

AirP is FlairOS V2's native package manager. It manages both FlairOS application containers and ordinary system packages.

## Package types

### FlairOS applications: `.airp`

An `.airp` file is an application container. AirP installs it under `/opt/flairos/apps/` and tracks it for listing, inspection, and removal. The AIRP application execution/compatibility runtime is a separate component and is not yet part of this Linux-foundation milestone.

### System packages: `.airpkg`

An `.airpkg` file is a gzip-compressed tar archive containing:

- `manifest` with at least `name=` and `version=` fields
- `rootfs/` containing files placed at the root of the FlairOS filesystem

Example manifest:

```ini
name=hello-tools
version=1.0
description=Example FlairOS system package
```

AirP rejects absolute paths, parent-directory traversal, symbolic and hard links, and special files in system package archives. Installed file paths are recorded under `/var/lib/airp/packages/` for removal.

## Commands

```sh
airp install ./application.airp
airp install ./system-package.airpkg
airp list
airp info hello-tools
airp remove hello-tools
```

The first implementation supports local package files. A signed remote repository/index, dependency resolution, package upgrades, rollback, and the AIRP application runtime are future work; they are not silently assumed to exist in this initial foundation.
