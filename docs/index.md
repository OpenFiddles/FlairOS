# FlairOS Developer Hub

Welcome to the **FlairOS** developer documentation, maintained by **OpenFiddles**.

This site brings together the project's architecture notes, boot and storage
guides, application SDK documentation, and automatically generated C API
reference.

## Explore

- **[Getting Started](GETTING_STARTED.md)** — build the project and find the main development entry points.
- **[Architecture](ARCHITECTURE.md)** — understand the current kernel and desktop boundaries.
- **[AirGraph](AirGraph.md)** — graphics surfaces, renderer backends, and display profiles.
- **[Memory Management](MEMORY_MANAGEMENT.md)** — PMM, VMM, virtual regions, and current limitations.
- **[Boot Media](BOOT_MEDIA.md)** — LiveCD, Setup BootCD, and legacy disk images.
- **[Pancake / PCK](PANCAKE.md)** — the resource-bundle format and host utility.
- **[Application SDK](SDK.md)** — build ring-3 applications for FlairOS.

## API reference

The API reference is generated from C headers and source files during the
GitHub Pages build.

[Open the generated Doxygen API reference](api/index.html).

## Project links

- [GitHub repository](https://github.com/OpenFiddles/FlairOS)
- [OpenFiddles on GitLab](https://gitlab.com/openfiddles/FlairOS)

Documentation describes the implementation represented by the current source.
Items marked *planned*, *experimental*, or *future* are not claims that those
capabilities are complete.
