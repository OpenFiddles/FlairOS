cmake_minimum_required(VERSION 3.20)
project(airos C)

if(NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "i686")
  message(FATAL_ERROR "AirOS builds only i686 for now; x86-64 and ARM need their own boot layer")
endif()

find_program(AIROS_NASM nasm REQUIRED)
find_program(AIROS_OBJCOPY NAMES llvm-objcopy objcopy REQUIRED)
find_program(AIROS_PYTHON NAMES python3 python REQUIRED)
find_program(AIROS_QEMU qemu-system-i386)

set(B ${CMAKE_BINARY_DIR})
set(AIROS_INCLUDES ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/kernel)
set(AIROS_CFLAGS -std=gnu99 -ffreestanding -mno-sse -mno-sse2 -mno-mmx -fno-pic -fno-stack-protector -fno-builtin
    -fno-common -fcf-protection=none -fno-asynchronous-unwind-tables -Os -ffunction-sections -fdata-sections -Wall -Wextra)

add_custom_command(OUTPUT ${B}/stage1.bin
  COMMAND ${AIROS_NASM} -f bin ${CMAKE_SOURCE_DIR}/bootloader/stage1.asm -o ${B}/stage1.bin
  DEPENDS ${CMAKE_SOURCE_DIR}/bootloader/stage1.asm VERBATIM)
add_custom_command(OUTPUT ${B}/stage2.bin
  COMMAND ${AIROS_NASM} -f bin ${CMAKE_SOURCE_DIR}/bootloader/stage2.asm -o ${B}/stage2.bin
  COMMAND truncate -s 2048 ${B}/stage2.bin
  DEPENDS ${CMAKE_SOURCE_DIR}/bootloader/stage2.asm VERBATIM)

set(AIROS_ASM_OBJS)
foreach(src IN ITEMS kernel/entry.asm kernel/isr.asm kernel/usermode.asm)
  get_filename_component(stem ${src} NAME_WE)
  set(obj ${B}/${stem}.o)
  add_custom_command(OUTPUT ${obj}
    COMMAND ${AIROS_NASM} -f elf32 ${CMAKE_SOURCE_DIR}/${src} -o ${obj}
    DEPENDS ${CMAKE_SOURCE_DIR}/${src} VERBATIM)
  list(APPEND AIROS_ASM_OBJS ${obj})
endforeach()
set_source_files_properties(${AIROS_ASM_OBJS} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)

file(GLOB_RECURSE AIROS_DRIVER_SRCS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/drivers/*.c)
set(AIROS_KERNEL_SRCS
  arch/i386/hal.c
  kernel/desktop.c kernel/desktop_gfx.c kernel/desktop_render.c kernel/desktop_chrome.c kernel/font5x7.c
  kernel/kernel.c kernel/drivers.c kernel/keyboard.c kernel/panic.c
  kernel/mouse_cursor.c kernel/mouse_init.c kernel/pit_timer.c
  kernel/axi.c kernel/fs.c kernel/sysdispatch.c kernel/boot.c
  kernel/hwinfo.c kernel/setup.c kernel/airp.c kernel/fb.c kernel/vbe.c
  kernel/airgraph.c kernel/sprunkui.c kernel/bmp.c)
list(TRANSFORM AIROS_KERNEL_SRCS PREPEND ${CMAKE_SOURCE_DIR}/)

add_library(airos_kernel_c OBJECT ${AIROS_KERNEL_SRCS} ${AIROS_DRIVER_SRCS})
target_compile_options(airos_kernel_c PRIVATE ${AIROS_CFLAGS})
target_include_directories(airos_kernel_c PRIVATE ${AIROS_INCLUDES})

add_executable(kernel ${AIROS_ASM_OBJS} $<TARGET_OBJECTS:airos_kernel_c>)
set_target_properties(kernel PROPERTIES OUTPUT_NAME kernel SUFFIX .elf
  LINK_DEPENDS ${CMAKE_SOURCE_DIR}/kernel/link.ld)
target_link_options(kernel PRIVATE --gc-sections -T ${CMAKE_SOURCE_DIR}/kernel/link.ld)

add_custom_command(OUTPUT ${B}/kernel.bin
  COMMAND ${AIROS_OBJCOPY} -O binary $<TARGET_FILE:kernel> ${B}/kernel.bin
  COMMAND sh -c "test $(stat -c %s ${B}/kernel.bin) -le 131072"
  COMMAND truncate -s 131072 ${B}/kernel.bin
  DEPENDS kernel VERBATIM)

add_library(airos_rt OBJECT ${CMAKE_SOURCE_DIR}/userland/rt.c)
target_compile_options(airos_rt PRIVATE ${AIROS_CFLAGS})
target_include_directories(airos_rt PRIVATE ${AIROS_INCLUDES})

function(airos_app name src max_bytes pad)
  add_executable(${name} ${src} $<TARGET_OBJECTS:airos_rt>)
  target_compile_options(${name} PRIVATE ${AIROS_CFLAGS})
  target_include_directories(${name} PRIVATE ${AIROS_INCLUDES})
  set_target_properties(${name} PROPERTIES SUFFIX .elf LINK_DEPENDS ${CMAKE_SOURCE_DIR}/userland/app.ld)
  target_link_options(${name} PRIVATE -T ${CMAKE_SOURCE_DIR}/userland/app.ld)
  if(pad)
    add_custom_command(OUTPUT ${B}/${name}.bin
      COMMAND ${AIROS_OBJCOPY} -O binary $<TARGET_FILE:${name}> ${B}/${name}.bin
      COMMAND sh -c "test $(stat -c %s ${B}/${name}.bin) -le ${max_bytes}"
      COMMAND truncate -s ${max_bytes} ${B}/${name}.bin
      DEPENDS ${name} VERBATIM)
  else()
    add_custom_command(OUTPUT ${B}/${name}.bin
      COMMAND ${AIROS_OBJCOPY} -O binary $<TARGET_FILE:${name}> ${B}/${name}.bin
      COMMAND sh -c "test $(stat -c %s ${B}/${name}.bin) -le ${max_bytes}"
      DEPENDS ${name} VERBATIM)
  endif()
endfunction()

airos_app(app ${CMAKE_SOURCE_DIR}/userland/app.c 65536 TRUE)
airos_app(explorer ${CMAKE_SOURCE_DIR}/userland/explorer.c 16348 FALSE)
airos_app(terminal ${CMAKE_SOURCE_DIR}/userland/terminal/terminal.c 32732 FALSE)
airos_app(win_app ${CMAKE_SOURCE_DIR}/userland/win_app.c 65536 TRUE)

add_custom_command(OUTPUT ${B}/explorer.airp
  COMMAND ${AIROS_PYTHON} ${CMAKE_SOURCE_DIR}/tools/airpack.py ${B}/explorer.bin ${B}/explorer.airp explorer
  COMMAND sh -c "test $(stat -c %s ${B}/explorer.airp) -le 16384"
  DEPENDS ${B}/explorer.bin ${CMAKE_SOURCE_DIR}/tools/airpack.py VERBATIM)

add_custom_command(OUTPUT ${B}/terminal.airp
  COMMAND ${AIROS_PYTHON} ${CMAKE_SOURCE_DIR}/tools/airpack.py ${B}/terminal.bin ${B}/terminal.airp terminal
  COMMAND sh -c "test $(stat -c %s ${B}/terminal.airp) -le 32768"
  DEPENDS ${B}/terminal.bin ${CMAKE_SOURCE_DIR}/tools/airpack.py VERBATIM)

add_custom_command(OUTPUT ${B}/textile.img
  COMMAND dd if=/dev/zero of=${B}/textile.img bs=512 count=2880 status=none
  COMMAND dd if=${B}/stage1.bin of=${B}/textile.img bs=512 count=1 conv=notrunc status=none
  COMMAND dd if=${B}/stage2.bin of=${B}/textile.img bs=512 seek=1 count=4 conv=notrunc status=none
  COMMAND dd if=${B}/kernel.bin of=${B}/textile.img bs=512 seek=5 count=256 conv=notrunc status=none
  COMMAND dd if=${B}/app.bin of=${B}/textile.img bs=512 seek=261 count=128 conv=notrunc status=none
  COMMAND dd if=${B}/explorer.airp of=${B}/textile.img bs=512 seek=2447 count=32 conv=notrunc status=none
  COMMAND dd if=${B}/terminal.airp of=${B}/textile.img bs=512 seek=2479 count=64 conv=notrunc status=none
  DEPENDS ${B}/stage1.bin ${B}/stage2.bin ${B}/kernel.bin ${B}/app.bin ${B}/explorer.airp ${B}/terminal.airp VERBATIM)

add_custom_target(image ALL DEPENDS ${B}/textile.img)

add_custom_target(smoke
  COMMAND ${AIROS_PYTHON} ${CMAKE_SOURCE_DIR}/tests/smoke.py ${B}/textile.img
  DEPENDS image USES_TERMINAL)

if(AIROS_QEMU)
  add_custom_target(run
    COMMAND ${AIROS_QEMU} -fda ${B}/textile.img -serial stdio -no-reboot
    DEPENDS image USES_TERMINAL)
endif()
