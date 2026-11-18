# FlairOS image assets

Place custom BMP assets here for the desktop and boot artwork.

- `BOOT.BMP`: optional boot artwork.
- `WALL.BMP`: optional desktop wallpaper.
- Additional BMP files can be used by components that load assets through the
  shared image subsystem and AirGraph.

Supported BMP input is uncompressed BI_RGB, 24-bit or 32-bit, with either
bottom-up or top-down row order. The shared decoder is in
`include/image.h` / `kernel/image.c`; AirGraph's `ag_draw_image()` draws a
decoded image using its normal clipping and blit path.

Decoded output is packed `0x00RRGGBB`. Callers provide the destination buffer;
the decoder validates dimensions, pitch, and capacity and does not allocate
memory internally.
