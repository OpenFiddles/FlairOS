resources/images/ holds the images the OS loads at boot.

BOOT_LOGO.bmp      boot screen logo (24-bit or 32-bit BMP)
icon_menubar.bmp   menu bar icon (24-bit or 32-bit BMP)

Upload the .bmp files here. Nothing reads them until the graphics mode exists; include/bmp.h checks their headers.
