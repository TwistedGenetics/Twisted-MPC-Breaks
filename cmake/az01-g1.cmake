# Cross build for G1 MPCs and the Force (az01: Rockchip RK3288, Cortex-A17, armhf) with the toolchain that matches
# MPC OS 3.10: GCC 13.4 and glibc 2.39 as inMusic ships them (toolchain/build.sh, release toolchain-az01-g1-3.10).
#   cmake -B build-arm -DCMAKE_TOOLCHAIN_FILE=cmake/az01-g1.cmake -DTBM_AZ01=<az01-g1-3.10 folder>
# A plugin built with it needs glibc 2.39: MPC OS 3.x. (cmake/armv7-mpc.cmake, glibc 2.27, also loads on 2.x.)
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR armv7l)

if(NOT TBM_AZ01 AND DEFINED ENV{TBM_AZ01})
    set(TBM_AZ01 $ENV{TBM_AZ01})
endif()
if(NOT TBM_AZ01)
    message(FATAL_ERROR "Set TBM_AZ01 to the extracted az01-g1-3.10 toolchain folder")
endif()
set(TBM_AZ01 ${TBM_AZ01} CACHE PATH "MPC G1 (MPC OS 3.10) cross toolchain" FORCE)
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES TBM_AZ01)

set(_triple arm-az01-linux-gnueabihf)
set(CMAKE_C_COMPILER   ${TBM_AZ01}/bin/${_triple}-gcc)
set(CMAKE_CXX_COMPILER ${TBM_AZ01}/bin/${_triple}-g++)
set(CMAKE_SYSROOT      ${TBM_AZ01}/${_triple}/sysroot)

# the compiler's own defaults; spelled out so the build log says what the code is for
set(CMAKE_C_FLAGS_INIT   "-march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard -mtune=cortex-a17")
set(CMAKE_CXX_FLAGS_INIT "-march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard -mtune=cortex-a17")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
