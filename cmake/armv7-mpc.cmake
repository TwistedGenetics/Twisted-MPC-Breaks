# Cross build for Gen1 MPCs and the Force (Rockchip RK3288, Cortex-A17, armhf).
# Toolchain: tttapa/toolchains armv7-neon-linux-gnueabihf (crosstool-ng, glibc 2.27,
# libstdc++ built with -fPIC so it links statically into the plugin). glibc 2.27 keeps
# the plugin loadable on MPC OS 2.x (glibc 2.32) as well as 3.x (2.39).
#   cmake -B build-arm -DCMAKE_TOOLCHAIN_FILE=cmake/armv7-mpc.cmake -DTBM_XTOOLS=<x-tools/armv7-neon-linux-gnueabihf>
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR armv7l)

if(NOT TBM_XTOOLS AND DEFINED ENV{TBM_XTOOLS})
    set(TBM_XTOOLS $ENV{TBM_XTOOLS})
endif()
if(NOT TBM_XTOOLS)
    message(FATAL_ERROR "Set TBM_XTOOLS to the extracted x-tools/armv7-neon-linux-gnueabihf folder")
endif()
set(TBM_XTOOLS ${TBM_XTOOLS} CACHE PATH "armv7 cross toolchain" FORCE)
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES TBM_XTOOLS)

set(_triple armv7-neon-linux-gnueabihf)
set(CMAKE_C_COMPILER   ${TBM_XTOOLS}/bin/${_triple}-gcc)
set(CMAKE_CXX_COMPILER ${TBM_XTOOLS}/bin/${_triple}-g++)
set(CMAKE_SYSROOT      ${TBM_XTOOLS}/${_triple}/sysroot)

set(CMAKE_C_FLAGS_INIT   "-march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard -mtune=cortex-a17")
set(CMAKE_CXX_FLAGS_INIT "-march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard -mtune=cortex-a17")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
