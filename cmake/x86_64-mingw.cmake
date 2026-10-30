# Toolchain for a native 64-bit Windows build (opensmac_headless.exe) with the local llvm-mingw.
#   cmake -S . -B build/win64 -DCMAKE_TOOLCHAIN_FILE=cmake/x86_64-mingw.cmake
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(LLVM_MINGW ${CMAKE_CURRENT_LIST_DIR}/../tools/llvm-mingw-20260922-msvcrt-ubuntu-22.04-x86_64)
set(CMAKE_C_COMPILER ${LLVM_MINGW}/bin/x86_64-w64-mingw32-clang)
set(CMAKE_CXX_COMPILER ${LLVM_MINGW}/bin/x86_64-w64-mingw32-clang++)
set(CMAKE_RC_COMPILER ${LLVM_MINGW}/bin/x86_64-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH ${LLVM_MINGW}/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
# Self-contained executables (no libc++/libunwind DLLs next to them).
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")
