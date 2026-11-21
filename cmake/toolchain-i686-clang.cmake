# Freestanding i686 target built with clang. Termux's clang driver links through the host gcc,
# which cannot produce i686 ELF, so the link rule calls ld.lld directly.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR i686)
set(CMAKE_C_COMPILER clang)
set(CMAKE_C_COMPILER_TARGET i686-unknown-elf)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_C_LINK_EXECUTABLE "ld.lld -m elf_i386 <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")
