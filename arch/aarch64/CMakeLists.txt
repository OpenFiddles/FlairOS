# Native ARM64 kernel only; deliberately independent from the i686/v86 image.
if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
  message(FATAL_ERROR "Configure AArch64 with cmake/toolchains/aarch64-linux.cmake")
endif()

add_executable(aarch64_kernel start.S kernel.c)
set_target_properties(aarch64_kernel PROPERTIES
  OUTPUT_NAME kernel
  SUFFIX .elf
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/aarch64)
target_compile_options(aarch64_kernel PRIVATE
  -ffreestanding -fno-stack-protector -fno-pic -fno-builtin
  -mgeneral-regs-only -Wall -Wextra -O2 -mcpu=cortex-a72)
target_link_options(aarch64_kernel PRIVATE
  -nostdlib -static -Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/linker.ld
  -Wl,--build-id=none)
