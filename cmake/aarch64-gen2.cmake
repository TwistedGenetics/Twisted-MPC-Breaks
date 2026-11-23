# Cross build for Gen2 MPCs (MPC Live III, XL, One G2, Key 37 G2: Rockchip RK3588 / RK3588S, Cortex-A76 + A55, aarch64).
# Their MPC OS 3.x userland is 64-bit only with glibc 2.39 (toolkit docs/GEN2.md), so a Gen1 armv7 build can't load there.
# Ubuntu 24.04's cross compiler (g++-aarch64-linux-gnu: GCC 13, glibc 2.39 in /usr/aarch64-linux-gnu) matches that ceiling:
#   sudo apt-get install g++-aarch64-linux-gnu
#   cmake -B build-a64 -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-gen2.cmake -DTBM_X11_INCLUDE=<dir with X11/>
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(_triple aarch64-linux-gnu)
find_program(_cc NAMES ${_triple}-gcc-13 ${_triple}-gcc REQUIRED)
find_program(_cxx NAMES ${_triple}-g++-13 ${_triple}-g++ REQUIRED)
set(CMAKE_C_COMPILER   ${_cc})
set(CMAKE_CXX_COMPILER ${_cxx})
set(CMAKE_FIND_ROOT_PATH /usr/${_triple})

# armv8-a runs on both cores of every Gen2 board; tuned for the big A76s that MPC's audio threads use
set(CMAKE_C_FLAGS_INIT   "-march=armv8-a -mtune=cortex-a76")
set(CMAKE_CXX_FLAGS_INIT "-march=armv8-a -mtune=cortex-a76")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
