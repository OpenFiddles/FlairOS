# Freestanding i686 target built with clang. Termux's clang driver links through the host gcc,
# which cannot produce i686 ELF, so the link rule calls ld.lld directly.
#
# clang emits noticeably more code than gcc at -Os, and kernel/boot/link.ld asserts
# that the image plus bss fits below 0x30000 (the reserved PMM bitmap and Explorer
# staging gap). With clang -Os that assertion fails by ~2 KiB; -Oz brings the
# image back under it with margin. The assertion is not relaxed and the gcc path
# (cmake/i386-toolchain.cmake, the Makefile) keeps -Os.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR i686)
set(CMAKE_C_COMPILER clang)
set(CMAKE_C_COMPILER_TARGET i686-unknown-elf)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_C_LINK_EXECUTABLE "ld.lld -m elf_i386 <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")
if(NOT FLAIROS_TOOLCHAIN_CFLAGS)
    set(FLAIROS_TOOLCHAIN_CFLAGS "-Oz" CACHE STRING "Extra kernel CFLAGS for this toolchain" FORCE)
endif()
