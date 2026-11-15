# Native ARM64 kernel only; deliberately independent from the i686/v86 image.
if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
  message(FATAL_ERROR "Configure AArch64 with cmake/toolchains/aarch64-linux.cmake")
endif()

find_program(FLAIROS_QEMU_AARCH64 qemu-system-aarch64)

add_executable(aarch64_kernel start.S kernel.c)
set_target_properties(aarch64_kernel PROPERTIES
  OUTPUT_NAME kernel
  SUFFIX .elf
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/aarch64)
target_compile_options(aarch64_kernel PRIVATE
  -ffreestanding -fno-stack-protector -fno-pic -fno-builtin
  -mgeneral-regs-only -ffunction-sections -fdata-sections
  -Wall -Wextra -Werror -O2 -mcpu=cortex-a72)
target_link_options(aarch64_kernel PRIVATE
  -nostdlib -static -Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/linker.ld
  -Wl,--gc-sections -Wl,--build-id=none
  -Wl,-Map,${CMAKE_BINARY_DIR}/aarch64/kernel.map)

if(FLAIROS_QEMU_AARCH64)
  add_custom_target(aarch64_run
    COMMAND ${FLAIROS_QEMU_AARCH64} -M virt -cpu cortex-a72 -m 256M
            -nographic -nic none -no-reboot
            -kernel $<TARGET_FILE:aarch64_kernel>
    DEPENDS aarch64_kernel USES_TERMINAL)
  add_custom_target(aarch64_debug
    COMMAND ${FLAIROS_QEMU_AARCH64} -M virt -cpu cortex-a72 -m 256M
            -nographic -nic none -no-reboot -S -s
            -kernel $<TARGET_FILE:aarch64_kernel>
    DEPENDS aarch64_kernel USES_TERMINAL)
else()
  message(STATUS "qemu-system-aarch64 not found; aarch64_run/aarch64_debug targets unavailable")
endif()
