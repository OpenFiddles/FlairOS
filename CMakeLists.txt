cmake_minimum_required(VERSION 3.20)
project(FlairOS C ASM)

enable_testing()

set(FLAIROS_ARCH "i386" CACHE STRING "Target architecture (only i386 supported)")
set(FLAIROS_DEVICE "desktop" CACHE STRING "Device profile: desktop|phone|tablet|laptop|tv")
set(FLAIROS_DISPLAY "landscape" CACHE STRING "Display profile: landscape|portrait|wide|4k")
set(FLAIROS_SHELL "desktop" CACHE STRING "Shell: desktop|mobile")
option(FLAIROS_DEBUG "Enable debug logging and Ctrl+Alt+D SYS.LOG dump" ON)

if(FLAIROS_ARCH STREQUAL "aarch64")
    set(B "${CMAKE_CURRENT_BINARY_DIR}")
    set(S "${CMAKE_CURRENT_SOURCE_DIR}")
    if(NOT FLAIROS_LD)
        find_program(FLAIROS_LD NAMES ld.lld ld REQUIRED)
    endif()
    if(NOT FLAIROS_OBJCOPY)
        find_program(FLAIROS_OBJCOPY NAMES llvm-objcopy objcopy REQUIRED)
    endif()
    add_library(arm64_objs OBJECT
        "${S}/arch/aarch64/boot.S"
        "${S}/arch/aarch64/hal.c"
        "${S}/arch/aarch64/hello.c"
        "${S}/kernel/pkg/dex.c"
        "${S}/kernel/pkg/apk.c"
    )
    target_include_directories(arm64_objs PRIVATE "${S}/include")
    target_compile_definitions(arm64_objs PRIVATE FLAIROS_DEVICE_PHONE FLAIROS_DISPLAY_PORTRAIT)
    target_compile_options(arm64_objs PRIVATE $<$<COMPILE_LANGUAGE:C>:-std=gnu99 -ffreestanding -fno-pic -fno-stack-protector -Os -Wall -Wextra>)
    add_custom_command(
        OUTPUT "${B}/flairos-arm64.elf"
        COMMAND "${FLAIROS_LD}" -m aarch64elf -nostdlib -T "${S}/arch/aarch64/linker.ld"
                -o "${B}/flairos-arm64.elf" $<TARGET_OBJECTS:arm64_objs>
        DEPENDS arm64_objs "${S}/arch/aarch64/linker.ld"
        COMMENT "Linking flairos-arm64.elf"
        COMMAND_EXPAND_LISTS
    )
    add_custom_command(
        OUTPUT "${B}/flairos-arm64.bin"
        COMMAND "${FLAIROS_OBJCOPY}" -O binary "${B}/flairos-arm64.elf" "${B}/flairos-arm64.bin"
        DEPENDS "${B}/flairos-arm64.elf"
        COMMENT "Creating flairos-arm64.bin"
    )
    add_custom_target(flairos-arm64 ALL DEPENDS "${B}/flairos-arm64.bin")
    return()
elseif(NOT FLAIROS_ARCH STREQUAL "i386")
    message(FATAL_ERROR "Unsupported FLAIROS_ARCH=${FLAIROS_ARCH} (use i386 or aarch64).")
endif()

find_program(NASM_BIN nasm REQUIRED)
find_program(PYTHON3_BIN python3 REQUIRED)
find_program(HOST_CC NAMES cc clang gcc REQUIRED)
find_program(QEMU_BIN qemu-system-i386)
find_program(XORRISO_BIN xorriso)

if(NOT FLAIROS_LD)
    find_program(FLAIROS_LD NAMES ld.lld ld REQUIRED)
endif()
if(NOT FLAIROS_OBJCOPY)
    find_program(FLAIROS_OBJCOPY NAMES llvm-objcopy objcopy REQUIRED)
endif()

set(B "${CMAKE_CURRENT_BINARY_DIR}")
set(S "${CMAKE_CURRENT_SOURCE_DIR}")

# Device & display compile definitions
set(FLAIROS_DEFS "")
if(FLAIROS_DEVICE STREQUAL "phone")
    list(APPEND FLAIROS_DEFS FLAIROS_DEVICE_PHONE)
elseif(FLAIROS_DEVICE STREQUAL "tablet")
    list(APPEND FLAIROS_DEFS FLAIROS_DEVICE_TABLET)
elseif(FLAIROS_DEVICE STREQUAL "laptop")
    list(APPEND FLAIROS_DEFS FLAIROS_DEVICE_LAPTOP)
elseif(FLAIROS_DEVICE STREQUAL "tv")
    list(APPEND FLAIROS_DEFS FLAIROS_DEVICE_TV)
endif()

if(FLAIROS_DEBUG)
    list(APPEND FLAIROS_DEFS FLAIROS_DEBUG)
endif()

if(FLAIROS_SHELL STREQUAL "mobile")
    if(NOT FLAIROS_DEVICE STREQUAL "phone" OR NOT FLAIROS_DISPLAY STREQUAL "portrait")
        message(FATAL_ERROR "The mobile shell currently requires FLAIROS_DEVICE=phone and FLAIROS_DISPLAY=portrait.")
    endif()
    list(APPEND FLAIROS_DEFS FLAIROS_SHELL_MOBILE)
elseif(NOT FLAIROS_SHELL STREQUAL "desktop")
    message(FATAL_ERROR "Unsupported FLAIROS_SHELL=${FLAIROS_SHELL} (use desktop or mobile).")
endif()

if(FLAIROS_DISPLAY STREQUAL "portrait")
    list(APPEND FLAIROS_DEFS FLAIROS_DISPLAY_PORTRAIT)
elseif(FLAIROS_DISPLAY STREQUAL "wide")
    list(APPEND FLAIROS_DEFS FLAIROS_DISPLAY_WIDE)
elseif(FLAIROS_DISPLAY STREQUAL "4k")
    list(APPEND FLAIROS_DEFS FLAIROS_DISPLAY_4K)
endif()

set(FLAIROS_CFLAGS
    -m32
    -std=gnu99
    -ffreestanding
    -mno-sse
    -mno-sse2
    -mno-mmx
    -fno-pic
    -fno-stack-protector
    -fno-builtin
    -fno-common
    -fcf-protection=none
    -fno-asynchronous-unwind-tables
    -Os
    -ffunction-sections
    -fdata-sections
    -Wall
    -Wextra
)
if(FLAIROS_TOOLCHAIN_CFLAGS)
    list(APPEND FLAIROS_CFLAGS ${FLAIROS_TOOLCHAIN_CFLAGS})
endif()

# --- Bootloader Stages ---
file(GLOB BOOTLOADER_INCLUDES CONFIGURE_DEPENDS
    "${S}/legacy/i386/bootloader/*.inc" "${S}/legacy/i386/bootloader/loader/*.inc")
add_custom_command(
    OUTPUT "${B}/stage1.bin"
    COMMAND "${NASM_BIN}" -f bin -I "${S}/legacy/i386/bootloader/" "${S}/legacy/i386/bootloader/stage1.asm" -o "${B}/stage1.bin"
    DEPENDS "${S}/legacy/i386/bootloader/stage1.asm" ${BOOTLOADER_INCLUDES}
    COMMENT "Assembling stage1.bin"
)

macro(add_stage2_variant TARGET_FILE MODE)
    add_custom_command(
        OUTPUT "${B}/${TARGET_FILE}"
        COMMAND "${NASM_BIN}" -f bin -I "${S}/legacy/i386/bootloader/" -D "FORCE_BOOT_MODE=${MODE}" "${S}/legacy/i386/bootloader/stage2.asm" -o "${B}/${TARGET_FILE}"
        COMMAND "${PYTHON3_BIN}" "${S}/tools/check_size.py" "${B}/${TARGET_FILE}" 3072 3072
        DEPENDS "${S}/legacy/i386/bootloader/stage2.asm" ${BOOTLOADER_INCLUDES}
        COMMENT "Assembling ${TARGET_FILE}"
    )
endmacro()

add_stage2_variant("stage2.bin" 0)
add_stage2_variant("stage2-live.bin" 1)
add_stage2_variant("stage2-setup.bin" 2)

# --- Kernel Assembly Objects ---
set(KERNEL_ASM_OBJS "")
foreach(asm_name entry isr usermode)
    add_custom_command(
        OUTPUT "${B}/${asm_name}.o"
        COMMAND "${NASM_BIN}" -f elf32 "${S}/kernel/boot/${asm_name}.asm" -o "${B}/${asm_name}.o"
        DEPENDS "${S}/kernel/boot/${asm_name}.asm"
        COMMENT "Assembling kernel/boot/${asm_name}.asm"
    )
    list(APPEND KERNEL_ASM_OBJS "${B}/${asm_name}.o")
endforeach()

# --- Kernel & Driver C Objects ---
file(GLOB_RECURSE DRV_SRCS CONFIGURE_DEPENDS "${S}/drivers/*.c")

set(KERNEL_C_SRCS
    "${S}/legacy/i386/arch/hal.c"
    "${S}/kernel/ui/desktop.c"
    "${S}/kernel/ui/mobile.c"
    "${S}/kernel/ui/desktop_gfx.c"
    "${S}/kernel/ui/desktop_render.c"
    "${S}/kernel/ui/desktop_chrome.c"
    "${S}/kernel/ui/windown.c"
    "${S}/kernel/core/task.c"
    "${S}/kernel/pkg/elf.c"
    "${S}/kernel/fs/vfs.c"
    "${S}/kernel/fs/fat32.c"
    "${S}/kernel/mm/ashmem.c"
    "${S}/kernel/mm/binder.c"
    "${S}/kernel/pkg/apk.c"
    "${S}/kernel/pkg/dex.c"
    "${S}/kernel/gfx/font5x7.c"
    "${S}/kernel/core/kernel.c"
    "${S}/kernel/core/drivers.c"
    "${S}/kernel/dev/keyboard.c"
    "${S}/kernel/core/panic.c"
    "${S}/kernel/dev/mouse_cursor.c"
    "${S}/kernel/dev/mouse_init.c"
    "${S}/kernel/dev/pit_timer.c"
    ${DRV_SRCS}
    "${S}/kernel/core/axi.c"
    "${S}/kernel/fs/fs.c"
    "${S}/kernel/core/sysdispatch.c"
    "${S}/kernel/boot/boot.c"
    "${S}/kernel/boot/hwinfo.c"
    "${S}/kernel/boot/setup.c"
    "${S}/kernel/pkg/airp.c"
    "${S}/kernel/gfx/fb.c"
    "${S}/kernel/gfx/vbe.c"
    "${S}/kernel/gfx/airgraph.c"
    "${S}/kernel/gfx/bmp.c"
    "${S}/kernel/gfx/image.c"
    "${S}/kernel/gfx/cursor.c"
    "${S}/kernel/gfx/sprunkui.c"
    "${S}/kernel/gfx/airnim.c"
    "${S}/kernel/gfx/status.c"
    "${S}/kernel/gfx/sprunkui_anim.c"
)

add_library(kernel_objs OBJECT ${KERNEL_C_SRCS})
target_include_directories(kernel_objs PRIVATE "${S}/include" "${S}/kernel")
target_compile_definitions(kernel_objs PRIVATE ${FLAIROS_DEFS})
target_compile_options(kernel_objs PRIVATE ${FLAIROS_CFLAGS})

add_custom_command(
    OUTPUT "${B}/kernel.elf"
    COMMAND "${FLAIROS_LD}" -m elf_i386 -nostdlib --gc-sections -T "${S}/kernel/boot/link.ld"
            -o "${B}/kernel.elf" ${KERNEL_ASM_OBJS} $<TARGET_OBJECTS:kernel_objs>
    DEPENDS ${KERNEL_ASM_OBJS} $<TARGET_OBJECTS:kernel_objs> "${S}/kernel/boot/link.ld"
    COMMENT "Linking kernel.elf"
    COMMAND_EXPAND_LISTS
)

add_custom_command(
    OUTPUT "${B}/kernel.bin"
    COMMAND "${FLAIROS_OBJCOPY}" -O binary "${B}/kernel.elf" "${B}/kernel.bin"
    COMMAND "${PYTHON3_BIN}" "${S}/tools/check_size.py" "${B}/kernel.bin" 131072 131072
    DEPENDS "${B}/kernel.elf"
    COMMENT "Creating kernel.bin (max 128 KiB)"
)

# --- Userland Runtime & Apps ---
add_library(user_rt OBJECT "${S}/userland/rt.c")
target_include_directories(user_rt PRIVATE "${S}/include" "${S}/kernel")
target_compile_definitions(user_rt PRIVATE ${FLAIROS_DEFS})
target_compile_options(user_rt PRIVATE ${FLAIROS_CFLAGS})

function(add_user_bin NAME SRC MAX_BYTES PAD_TO)
    add_library(${NAME}_obj OBJECT "${SRC}")
    target_include_directories(${NAME}_obj PRIVATE "${S}/include" "${S}/kernel" "${S}/sdk")
    target_compile_definitions(${NAME}_obj PRIVATE ${FLAIROS_DEFS})
    target_compile_options(${NAME}_obj PRIVATE ${FLAIROS_CFLAGS})

    add_custom_command(
        OUTPUT "${B}/${NAME}.elf"
        COMMAND "${FLAIROS_LD}" -m elf_i386 -nostdlib -T "${S}/userland/app.ld"
                -o "${B}/${NAME}.elf" $<TARGET_OBJECTS:${NAME}_obj> $<TARGET_OBJECTS:user_rt>
        DEPENDS ${NAME}_obj user_rt "${S}/userland/app.ld"
        COMMENT "Linking ${NAME}.elf"
        COMMAND_EXPAND_LISTS
    )
    if(PAD_TO GREATER 0)
        add_custom_command(
            OUTPUT "${B}/${NAME}.bin"
            COMMAND "${FLAIROS_OBJCOPY}" -O binary "${B}/${NAME}.elf" "${B}/${NAME}.bin"
            COMMAND "${PYTHON3_BIN}" "${S}/tools/check_size.py" "${B}/${NAME}.bin" "${MAX_BYTES}" "${PAD_TO}"
            DEPENDS "${B}/${NAME}.elf"
            COMMENT "Building ${NAME}.bin"
        )
    else()
        add_custom_command(
            OUTPUT "${B}/${NAME}.bin"
            COMMAND "${FLAIROS_OBJCOPY}" -O binary "${B}/${NAME}.elf" "${B}/${NAME}.bin"
            COMMAND "${PYTHON3_BIN}" "${S}/tools/check_size.py" "${B}/${NAME}.bin" "${MAX_BYTES}"
            DEPENDS "${B}/${NAME}.elf"
            COMMENT "Building ${NAME}.bin"
        )
    endif()
endfunction()

function(add_airp_app NAME SRC MAX_BIN_BYTES MAX_AIRP_BYTES)
    add_user_bin(${NAME} "${SRC}" ${MAX_BIN_BYTES} 0)
    add_custom_command(
        OUTPUT "${B}/${NAME}.airp"
        COMMAND "${PYTHON3_BIN}" "${S}/tools/airpack.py" "${B}/${NAME}.bin" "${B}/${NAME}.airp" "${NAME}"
        COMMAND "${PYTHON3_BIN}" "${S}/tools/check_size.py" "${B}/${NAME}.airp" "${MAX_AIRP_BYTES}"
        DEPENDS "${B}/${NAME}.bin" "${S}/tools/airpack.py"
        COMMENT "Packing ${NAME}.airp"
    )
endfunction()

add_user_bin(app "${S}/userland/app.c" 65536 65536)
add_user_bin(win_app "${S}/userland/win_app.c" 4096 0)
add_airp_app(explorer "${S}/userland/explorer.c" 16348 16384)
add_airp_app(terminal "${S}/userland/terminal/terminal.c" 32732 32768)
add_airp_app(settings "${S}/userland/settings.c" 16348 16384)
add_airp_app(notes "${S}/userland/notes.c" 8156 8192)

# Optional SDK examples
add_user_bin(hello "${S}/examples/hello/hello.c" 65536 0)
add_user_bin(libc_demo "${S}/examples/libc_demo/libc_demo.c" 65536 0)
add_custom_target(sdk-example DEPENDS "${B}/hello.bin" "${B}/libc_demo.bin")

# --- Disk Images ---
macro(add_disk_image IMG_NAME STAGE2_BIN)
    add_custom_command(
        OUTPUT "${B}/${IMG_NAME}"
        COMMAND dd if=/dev/zero of="${B}/${IMG_NAME}" bs=512 count=2880 status=none
        COMMAND dd if="${B}/stage1.bin" of="${B}/${IMG_NAME}" bs=512 count=1 conv=notrunc status=none
        COMMAND dd if="${B}/${STAGE2_BIN}" of="${B}/${IMG_NAME}" bs=512 seek=1 count=6 conv=notrunc status=none
        COMMAND dd if="${B}/kernel.bin" of="${B}/${IMG_NAME}" bs=512 seek=7 count=256 conv=notrunc status=none
        COMMAND dd if="${B}/app.bin" of="${B}/${IMG_NAME}" bs=512 seek=263 count=128 conv=notrunc status=none
        COMMAND dd if="${B}/explorer.airp" of="${B}/${IMG_NAME}" bs=512 seek=2447 count=32 conv=notrunc status=none
        COMMAND dd if="${B}/terminal.airp" of="${B}/${IMG_NAME}" bs=512 seek=2479 count=64 conv=notrunc status=none
        COMMAND dd if="${B}/settings.airp" of="${B}/${IMG_NAME}" bs=512 seek=2543 count=32 conv=notrunc status=none
        COMMAND dd if="${B}/notes.airp" of="${B}/${IMG_NAME}" bs=512 seek=2575 count=16 conv=notrunc status=none
        DEPENDS "${B}/stage1.bin" "${B}/${STAGE2_BIN}" "${B}/kernel.bin" "${B}/app.bin"
                "${B}/explorer.airp" "${B}/terminal.airp" "${B}/settings.airp" "${B}/notes.airp"
        COMMENT "Assembling ${IMG_NAME}"
    )
endmacro()

add_disk_image("textile.img" "stage2.bin")
add_disk_image("livecd.img" "stage2-live.bin")
add_disk_image("bootcd.img" "stage2-setup.bin")

add_custom_target(flairos-image ALL DEPENDS "${B}/textile.img")

add_custom_command(
    OUTPUT "${B}/storage.img"
    COMMAND truncate -s 31457280 "${B}/storage.img"
    COMMENT "Creating 30 MiB storage.img"
)
add_custom_target(storage-image DEPENDS "${B}/storage.img")

if(XORRISO_BIN)
    add_custom_command(
        OUTPUT "${B}/livecd.iso"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${B}/livecd-root"
        COMMAND "${CMAKE_COMMAND}" -E copy "${B}/livecd.img" "${B}/livecd-root/flair-livecd.img"
        COMMAND "${XORRISO_BIN}" -as mkisofs -quiet -V FLAIROS_LIVE -b flair-livecd.img -c boot.catalog -o "${B}/livecd.iso" "${B}/livecd-root"
        DEPENDS "${B}/livecd.img"
        COMMENT "Creating livecd.iso"
    )
    add_custom_command(
        OUTPUT "${B}/bootcd.iso"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${B}/bootcd-root"
        COMMAND "${CMAKE_COMMAND}" -E copy "${B}/bootcd.img" "${B}/bootcd-root/flair-bootcd.img"
        COMMAND "${XORRISO_BIN}" -as mkisofs -quiet -V FLAIROS_SETUP -b flair-bootcd.img -c boot.catalog -o "${B}/bootcd.iso" "${B}/bootcd-root"
        DEPENDS "${B}/bootcd.img"
        COMMENT "Creating bootcd.iso"
    )
    add_custom_target(boot-media DEPENDS "${B}/textile.img" "${B}/livecd.iso" "${B}/bootcd.iso")
endif()

if(QEMU_BIN)
    add_custom_target(run
        COMMAND "${QEMU_BIN}" -fda "${B}/textile.img" -hda "${B}/storage.img"
                -vga std -global VGA.vgamem_mb=64 -nic user,model=rtl8139 -serial stdio -no-reboot -m 256M
        DEPENDS "${B}/textile.img" "${B}/storage.img"
        USES_TERMINAL
    )
    add_custom_target(run-vnc
        COMMAND "${QEMU_BIN}" -fda "${B}/textile.img" -hda "${B}/storage.img"
                -vga std -global VGA.vgamem_mb=64 -vnc 0.0.0.0:0 -k en-us
                -nic user,model=rtl8139 -serial stdio -no-reboot -m 256M
        DEPENDS "${B}/textile.img" "${B}/storage.img"
        USES_TERMINAL
    )
endif()

# --- Host Tests (compiled with native HOST_CC, run via ctest) ---
set(HOST_TEST_BINS "")
function(add_flairos_host_test TEST_NAME)
    set(SRCS ${ARGN})
    set(ABS_SRCS "")
    foreach(src ${SRCS})
        list(APPEND ABS_SRCS "${S}/${src}")
    endforeach()
    set(EXTRA_FLAGS "")
    set(EXTRA_LIBS "")
    if(TEST_NAME STREQUAL "status_host_test" OR TEST_NAME STREQUAL "cursor_host_test" OR TEST_NAME STREQUAL "airnim_host_test" OR TEST_NAME STREQUAL "sprunkui_anim_host_test" OR TEST_NAME STREQUAL "telemetry_bridge_test" OR TEST_NAME STREQUAL "tv_drawer_host_test" OR TEST_NAME STREQUAL "mobile_host_test")
        list(APPEND EXTRA_FLAGS "-DFLAIROS_HOST_TEST")
    elseif(TEST_NAME STREQUAL "runtime64_airp_test")
        list(APPEND EXTRA_FLAGS "-DFLAIROS_HOST_TEST" "-DFLAIROS_64BIT"
             "-I${S}/ported/python/cpython/Include"
             "-I${S}/ported/python/build-cpython")
        if(EXISTS "${S}/ported/python/build-cpython/libpython3.16.a")
            list(APPEND EXTRA_FLAGS "-DFLAIROS_HAS_CPYTHON")
            list(APPEND EXTRA_LIBS "${S}/ported/python/build-cpython/libpython3.16.a" -liconv -ldl -llog -lm -lpthread)
        endif()
    endif()
    if(TEST_NAME STREQUAL "pmm_host_test" OR TEST_NAME STREQUAL "vm_region_host_test")
        list(APPEND EXTRA_FLAGS "-Werror")
    endif()
    add_custom_command(
        OUTPUT "${B}/${TEST_NAME}"
        COMMAND "${HOST_CC}" -std=gnu99 -Wall -Wextra ${EXTRA_FLAGS} "-I${S}/include" "-I${S}/kernel"
                -o "${B}/${TEST_NAME}" ${ABS_SRCS} ${EXTRA_LIBS}
        DEPENDS ${ABS_SRCS}
        COMMENT "Building host test ${TEST_NAME}"
    )
    set_property(GLOBAL APPEND PROPERTY FLAIROS_HOST_TEST_BINS "${B}/${TEST_NAME}")
    add_test(NAME "${TEST_NAME}" COMMAND "${B}/${TEST_NAME}")
endfunction()

add_flairos_host_test(airgraph_host_test tests/airgraph_host_test.c kernel/gfx/airgraph.c kernel/gfx/sprunkui.c kernel/gfx/fb.c)
add_flairos_host_test(status_host_test tests/status_host_test.c kernel/gfx/status.c)
add_flairos_host_test(sprunkui_anim_host_test tests/sprunkui_anim_host_test.c kernel/gfx/sprunkui_anim.c kernel/gfx/sprunkui.c kernel/gfx/airnim.c kernel/gfx/airgraph.c kernel/gfx/fb.c)
add_flairos_host_test(airnim_host_test tests/airnim_host_test.c kernel/gfx/airnim.c kernel/gfx/airgraph.c kernel/gfx/fb.c)
add_flairos_host_test(vfs_host_test tests/vfs_host_test.c)
add_flairos_host_test(elf_host_test tests/elf_host_test.c kernel/pkg/elf.c)
add_flairos_host_test(task_host_test tests/task_host_test.c)
add_flairos_host_test(windown_host_test tests/windown_host_test.c kernel/ui/windown.c kernel/ui/desktop_chrome.c kernel/gfx/font5x7.c kernel/gfx/airgraph.c kernel/gfx/fb.c)
add_flairos_host_test(desktop_chrome_host_test tests/desktop_chrome_host_test.c kernel/ui/desktop_chrome.c kernel/gfx/font5x7.c kernel/gfx/airgraph.c kernel/gfx/fb.c)
add_flairos_host_test(desktop_render_host_test tests/desktop_render_host_test.c kernel/ui/desktop_render.c kernel/gfx/font5x7.c kernel/gfx/airgraph.c kernel/gfx/fb.c kernel/gfx/bmp.c)
add_flairos_host_test(bmp_host_test tests/bmp_host_test.c kernel/gfx/bmp.c)
add_flairos_host_test(image_host_test tests/image_host_test.c kernel/gfx/image.c kernel/gfx/bmp.c)
add_flairos_host_test(driver_host_test tests/driver_host_test.c drivers/drv_core.c)
add_flairos_host_test(fb_host_test tests/fb_host_test.c kernel/gfx/fb.c)
add_flairos_host_test(ata_identify_host_test tests/ata_identify_host_test.c drivers/ata_identify.c)
add_flairos_host_test(pmm_host_test tests/pmm_host_test.c drivers/mm_core.c)
add_flairos_host_test(vm_region_host_test tests/vm_region_host_test.c drivers/vm_region.c)
add_flairos_host_test(cursor_host_test tests/cursor_host_test.c kernel/gfx/cursor.c kernel/gfx/bmp.c kernel/gfx/airgraph.c kernel/gfx/fb.c)
add_flairos_host_test(fat32_host_test tests/fat32_host_test.c kernel/fs/fat32.c)
add_flairos_host_test(ashmem_host_test tests/ashmem_host_test.c)
add_flairos_host_test(binder_host_test tests/binder_host_test.c)
add_flairos_host_test(apk_host_test tests/apk_host_test.c kernel/pkg/apk.c)
add_flairos_host_test(dex_host_test tests/dex_host_test.c kernel/pkg/dex.c kernel/pkg/apk.c)
add_flairos_host_test(telemetry_bridge_test tests/telemetry_bridge_test.c net/bridge/host_bridge.c net/service/net_service.c)
add_flairos_host_test(tv_host_test tests/tv_host_test.c kernel/ui/tv_layout.c kernel/ui/shell_launcher.c kernel/pkg/airp.c kernel/pkg/airjson.c)
add_flairos_host_test(mobile_host_test tests/mobile_host_test.c kernel/ui/mobile_layout.c kernel/ui/shell_launcher.c kernel/pkg/airp.c kernel/pkg/airjson.c)
add_flairos_host_test(tv_drawer_host_test tests/tv_drawer_host_test.c ui/systemui/airstin_ui.c kernel/ui/tv_layout.c kernel/ui/shell_launcher.c kernel/pkg/airp.c kernel/pkg/airjson.c graphics/compositor/glass_fx.c kernel/gfx/airgraph.c kernel/gfx/font5x7.c kernel/gfx/bmp.c kernel/gfx/fb.c)
add_flairos_host_test(bt_host_test tests/bt_host_test.c tests/bt_mock_hci.c drivers/bt/bt_hci.c net/bt/bt_core.c)
add_flairos_host_test(wifi_host_test tests/wifi_host_test.c net/wifi/wifi_ieee80211.c net/wifi/wpa2.c net/wifi/wifi_core.c)
add_flairos_host_test(wifi_join_host_test tests/wifi_join_host_test.c net/wifi/wifi_ieee80211.c net/wifi/wpa2.c net/wifi/wifi_core.c)
add_flairos_host_test(runtime64_airp_test tests/runtime64_airp_test.c tests/bt_mock_hci.c kernel/gfx/bmp.c kernel/fs/vfs.c kernel/mm/kmem64.c kernel/gfx/font5x7.c kernel/pkg/apk.c kernel/pkg/dex.c kernel/mm/binder.c kernel/mm/ashmem.c graphics/compositor/glass_fx.c runtime/art/art_bridge.c runtime/art/sandbox.c net/airp/airp_net.c drivers/bt/bt_hci.c net/bt/bt_core.c ported/developer/libs/flair_posix/flair_posix.c ported/developer/libs/flair_zlib/flair_zlib.c ported/developer/openjdk/src/jvm_class.c ported/developer/toybox/toybox_mini.c ported/developer/kilo/kilo_core.c ported/python/flair_py.c net/bridge/host_bridge.c kernel/pkg/elf64.c kernel/core/syscall64.c runtime/art/surface_flinger.c ui/shells/flair_browser.c runtime/art/dex_interp.c runtime/art/android_libs.c net/service/net_service.c ui/systemui/airstin_ui.c ui/shells/shell_desktop.c)

get_property(HOST_TEST_BINS GLOBAL PROPERTY FLAIROS_HOST_TEST_BINS)
add_custom_target(host-tests DEPENDS ${HOST_TEST_BINS})

add_test(
    NAME airp_apps_test
    COMMAND "${PYTHON3_BIN}" "${S}/tests/airp_apps_test.py"
            "${B}/explorer.airp" "${B}/settings.airp" "${B}/notes.airp"
)

