#!/usr/bin/env bash
# Builds a cross compiler and sysroot that match MPC OS 3.10 on G1 MPCs, from inMusic's open source release
# (toolchain/az01-g1-3.10.lock): binutils 2.42, GCC 13.4.0 (inMusic's tree), glibc 2.39 (inMusic's tree)
# against Linux 6.6 headers, and alsa-lib 1.2.11 in the sysroot.
#
#   toolchain/build.sh <work dir> <install prefix> [steps...]
#   steps: fetch binutils headers gcc1 glibc gcc2 alsa check package (default: all, in that order)
#
# The result: <prefix>/bin/arm-az01-linux-gnueabihf-{gcc,g++,ld,...}, sysroot <prefix>/arm-az01-linux-gnueabihf/sysroot.
# Each step leaves a stamp in the work dir, so a run can stop and carry on.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
LOCK="$HERE/az01-g1-3.10.lock"
WORK=$(mkdir -p "$1" && cd "$1" && pwd)
PREFIX=$(mkdir -p "$2" && cd "$2" && pwd)
shift 2
STEPS=${*:-fetch binutils headers gcc1 glibc gcc2 alsa check package}

TARGET=arm-az01-linux-gnueabihf
SYSROOT=$PREFIX/$TARGET/sysroot
JOBS=${JOBS:-$(nproc)}
# The G1 MPCs' CPU: RK3288, Cortex-A17 (ARMv7-A, VFPv4 + NEON), hard float. These are the compiler's defaults,
# so plain arm-az01-linux-gnueabihf-gcc builds for the device.
ARCH_FLAGS=(--with-arch=armv7-a --with-fpu=neon-vfpv4 --with-float=hard --with-tune=cortex-a17 --with-mode=arm)
export PATH=$PREFIX/bin:$PATH

log() { printf '\n=== %s\n' "$*"; }
stamp() { [ -f "$WORK/.done-$1" ]; }
done_() { touch "$WORK/.done-$1"; }

ref() { sed -n 's/^RELEASE_REF=//p' "$LOCK"; }
entry() { awk -v n="$1" '$1 == n { print $2, $3 }' "$LOCK"; }

fetch() {
    mkdir -p "$WORK/dl"
    for name in binutils gcc glibc headers alsa; do
        read -r sum where < <(entry "$name")
        file="$WORK/dl/$(basename "$where")"
        if [ ! -f "$file" ] || [ "$(sha256sum "$file" | cut -d' ' -f1)" != "$sum" ]; then
            case "$where" in
                http*) url=$where ;;
                *)     url="https://media.githubusercontent.com/media/inmusic-software/az0x-open-source/$(ref)/$where" ;;
            esac
            log "fetch $name: $url"
            curl -fsSL --retry 4 -o "$file.part" "$url"
            mv "$file.part" "$file"
        fi
        got=$(sha256sum "$file" | cut -d' ' -f1)
        [ "$got" = "$sum" ] || { echo "::error::$name: sha256 $got, the lock says $sum"; exit 1; }
        echo "ok $name $sum"
    done
    # unpack each into its own directory (the archives are inMusic's; nothing in them is run until a build step)
    rm -rf "$WORK/src" && mkdir -p "$WORK/src"
    tar xJf "$WORK/dl/binutils-2.42.tar.xz" -C "$WORK/src"
    for name in gcc glibc headers alsa; do
        read -r _ where < <(entry "$name")
        mkdir -p "$WORK/src/$name" && tar xJf "$WORK/dl/$(basename "$where")" -C "$WORK/src/$name"
    done
    # the trees say what the lock says
    grep -qx '13.4.0' "$WORK/src/gcc/gcc-13.4.0/gcc/BASE-VER"
    grep -q '#define VERSION "2.39"' "$WORK/src/glibc/git/version.h"
    [ "$(cat "$WORK/src/glibc/git/.git/refs/heads/release/2.39/master")" = ce65d944e38a20cb70af2a48a4b8aa5d8fabe1cc ]
    head -4 "$WORK/src/headers/linux-6.6/Makefile" | grep -q 'PATCHLEVEL = 6'
}

binutils() {
    log "binutils 2.42"
    rm -rf "$WORK/b-binutils" && mkdir -p "$WORK/b-binutils" && cd "$WORK/b-binutils"
    "$WORK/src/binutils-2.42/configure" --target=$TARGET --prefix="$PREFIX" --with-sysroot="$SYSROOT" \
        --disable-nls --disable-werror --disable-gdb --disable-gdbserver --disable-sim --disable-gprofng \
        --disable-multilib --enable-gold=no --enable-ld=default
    make -j"$JOBS"
    make install
}

headers() {
    log "Linux 6.6 headers (what the device's glibc was built against)"
    make -C "$WORK/src/headers/linux-6.6" ARCH=arm INSTALL_HDR_PATH="$SYSROOT/usr" headers_install >/dev/null
}

# inMusic's GCC tree carries Yocto's patches; one makes GCC take defaults.h from the build directory, where Yocto
# writes it with the system library directories first (gcc-configure-common.inc). The same, here.
gcc_defaults_h() {
    mkdir -p "$1/gcc"
    {
        echo '#ifndef SYSTEMLIBS_DIR'
        echo '# define SYSTEMLIBS_DIR "/lib/"'
        echo '#endif'
        echo '#ifndef STANDARD_STARTFILE_PREFIX_1'
        echo '# define STANDARD_STARTFILE_PREFIX_1 "/lib/"'
        echo '#endif'
        echo '#ifndef STANDARD_STARTFILE_PREFIX_2'
        echo '# define STANDARD_STARTFILE_PREFIX_2 "/usr/lib/"'
        echo '#endif'
        cat "$WORK/src/gcc/gcc-13.4.0/gcc/defaults.h"
    } > "$1/gcc/defaults.h"
}

gcc1() {
    log "GCC 13.4.0, stage 1 (C only, no libc yet)"
    rm -rf "$WORK/b-gcc1" && mkdir -p "$WORK/b-gcc1" && cd "$WORK/b-gcc1"
    gcc_defaults_h "$PWD"
    "$WORK/src/gcc/gcc-13.4.0/configure" --target=$TARGET --prefix="$PREFIX" --with-sysroot="$SYSROOT" \
        "${ARCH_FLAGS[@]}" --enable-languages=c --disable-multilib --disable-nls \
        --without-headers --with-newlib --with-glibc-version=2.39 --disable-shared --disable-threads \
        --disable-libssp --disable-libgomp --disable-libquadmath --disable-libatomic --disable-libsanitizer \
        --disable-libvtv --disable-libstdcxx --disable-bootstrap
    make -j"$JOBS" all-gcc all-target-libgcc
    make install-gcc install-target-libgcc
}

glibc() {
    log "glibc 2.39 (inMusic's tree), into the sysroot"
    rm -rf "$WORK/b-glibc" && mkdir -p "$WORK/b-glibc" && cd "$WORK/b-glibc"
    "$WORK/src/glibc/git/configure" --host=$TARGET --build="$(gcc -dumpmachine)" --prefix=/usr \
        --with-headers="$SYSROOT/usr/include" --enable-kernel=5.15 --disable-werror --disable-profile \
        --enable-stack-protector=strong --without-selinux --disable-crypt \
        libc_cv_slibdir=/lib libc_cv_rtlddir=/lib CC="$TARGET-gcc" CXX=false AR="$TARGET-ar" RANLIB="$TARGET-ranlib"
    make -j"$JOBS"
    make install DESTDIR="$SYSROOT"
}

gcc2() {
    log "GCC 13.4.0, final (C, C++, libstdc++ against the sysroot's glibc)"
    rm -rf "$WORK/b-gcc2" && mkdir -p "$WORK/b-gcc2" && cd "$WORK/b-gcc2"
    gcc_defaults_h "$PWD"
    # libstdc++.a / libgcc.a position-independent, so they link statically into a plugin (.so)
    CFLAGS_FOR_TARGET="-O2 -g -fPIC" CXXFLAGS_FOR_TARGET="-O2 -g -fPIC" \
    "$WORK/src/gcc/gcc-13.4.0/configure" --target=$TARGET --prefix="$PREFIX" --with-sysroot="$SYSROOT" \
        "${ARCH_FLAGS[@]}" --enable-languages=c,c++ --disable-multilib --disable-nls \
        --enable-shared --enable-threads=posix --enable-__cxa_atexit --enable-libstdcxx-time=yes \
        --enable-clocale=gnu --disable-libsanitizer --disable-libvtv --disable-bootstrap --enable-linker-build-id \
        --with-pkgversion="az01 G1 MPC OS 3.10 (Twisted Genetics)"
    make -j"$JOBS"
    make install
}

alsa() {
    log "alsa-lib 1.2.11 (inMusic's tree), into the sysroot"
    rm -rf "$WORK/b-alsa" && mkdir -p "$WORK/b-alsa" && cd "$WORK/b-alsa"
    src=$(ls -d "$WORK/src/alsa"/*/ | head -1)
    "$src/configure" --host=$TARGET --prefix=/usr --disable-python --disable-old-symbols --disable-topology \
        --with-configdir=/usr/share/alsa CC="$TARGET-gcc"
    make -j"$JOBS"
    make install DESTDIR="$SYSROOT"
    find "$SYSROOT/usr/lib" -name '*.la' -delete
}

check() {
    log "check: C and C++ for the device"
    d=$(mktemp -d)
    printf '#include <stdio.h>\n#include <gnu/libc-version.h>\nint main(void){printf("hello from glibc %%s\\n", gnu_get_libc_version());return 0;}\n' > "$d/hello.c"
    printf '#include <iostream>\n#include <thread>\n#include <alsa/asoundlib.h>\nint main(){std::thread t([]{std::cout << "hello from libstdc++, alsa-lib " << snd_asoundlib_version() << std::endl;});t.join();}\n' > "$d/hello.cpp"
    $TARGET-gcc -O2 "$d/hello.c" -o "$d/hello"
    $TARGET-g++ -O2 -std=c++17 "$d/hello.cpp" -o "$d/hellocc" -lasound -pthread
    printf '#include <string>\n#include <vector>\nextern "C" int plugin_entry(){std::vector<std::string> v{"a","b"};return (int)v.size();}\n' > "$d/plugin.cpp"
    $TARGET-g++ -O2 -shared -fPIC "$d/plugin.cpp" -o "$d/plugin.so" -static-libstdc++ -static-libgcc
    if readelf -d "$d/plugin.so" | grep -q 'libstdc++'; then echo "::error::static libstdc++ did not link in"; exit 1; fi
    echo "plugin.so: static libstdc++ links into a shared library"
    for b in hello hellocc; do
        readelf -l "$d/$b" | grep -q 'ld-linux-armhf.so.3' || { echo "::error::$b: not the hard-float loader"; exit 1; }
        readelf -A "$d/$b" | grep -q 'Tag_ABI_VFP_args: VFP registers' || { echo "::error::$b: not hard float"; exit 1; }
        hi=$(readelf -V "$d/$b" | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1)
        echo "$b: $(readelf -A "$d/$b" | sed -n 's/.*Tag_CPU_name: //p') / highest $hi"
    done
    if command -v qemu-arm >/dev/null; then
        qemu-arm -L "$SYSROOT" "$d/hello"
        LD_LIBRARY_PATH="$SYSROOT/lib:$SYSROOT/usr/lib:$PREFIX/$TARGET/lib" qemu-arm -L "$SYSROOT" "$d/hellocc"
    fi
    $TARGET-gcc --version | head -1
    rm -rf "$d"
}

package() {
    log "package"
    # libstdc++ / libgcc_s for the device live in <prefix>/<target>/lib; the sysroot gets them too, like the device
    cp -a "$PREFIX/$TARGET/lib/"lib{stdc++,gcc_s}*.so* "$SYSROOT/usr/lib/" 2>/dev/null || true
    find "$PREFIX/bin" "$PREFIX/libexec" -type f -perm -u+x -exec strip --strip-unneeded {} + 2>/dev/null || true
    cp "$LOCK" "$PREFIX/az01-g1-3.10.lock"
    {
        echo "arm-az01-linux-gnueabihf: a cross compiler matching MPC OS 3.10 on G1 MPCs (az01)."
        echo "Built by Twisted-MPC-Breaks toolchain/build.sh from inMusic's open source release (see az01-g1-3.10.lock)."
        echo "  $($TARGET-gcc --version | head -1)"
        echo "  $($TARGET-ld --version | head -1)"
        echo "  glibc 2.39 (sysroot), Linux 6.6 headers, alsa-lib 1.2.11"
        echo "Defaults: -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard -mtune=cortex-a17 (RK3288)."
    } > "$PREFIX/README.txt"
    out="$WORK/az01-g1-3.10-toolchain-x86_64.tar.xz"
    tar -C "$(dirname "$PREFIX")" -cJf "$out" "$(basename "$PREFIX")"
    ls -l "$out"
    sha256sum "$out" | tee "$out.sha256"
}

for s in $STEPS; do
    if stamp "$s" && [ "$s" != check ] && [ "$s" != package ]; then echo "(done: $s)"; continue; fi
    "$s"
    done_ "$s"
done
