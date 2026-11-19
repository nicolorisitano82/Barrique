#!/bin/zsh
# Builds FEX for Barrique from fex/ (this repository) with llvm-mingw:
#   <out>-arm64ec/Bin/libarm64ecfex.dll   x86-64 programs (ARM64EC)
#   <out>-wow64/Bin/libwow64fex.dll       32-bit x86 programs (WoW64)
# then points ~/Work/Personale/wine/fex-dlls at them (make-dist.sh copies from there).
#
#   build-fex.sh [output prefix]      default ~/Work/Personale/wine/fex-bq2
#
# HAS_CLANG_PRESERVE_ALL=0: FEX calls some helpers (x87 transcendental functions:
# fyl2x, fsin, f2xm1...) with clang's preserve_all convention and saves no registers
# around them. With llvm-mingw's clang (23.1.5) for ARM64EC that convention does not
# hold: an x86-64 program lost rbx/r12 across log() (Wine Gecko x64 crashed,
# tests/pe/x87regs.c shows it). FEX's CMake trusts preserve_all from clang 22.1 on;
# setting the result of its check to 0 makes FEX save the registers itself. This
# is build configuration only, FEX's code is unchanged.
set -e
W=$HOME/Work/Personale/wine
SRC=${0:A:h}/../fex
OUT=${1:-$W/fex-bq2}
export PATH=$W/llvm-mingw/bin:$PATH

build() {   # build <dir suffix> <triple> <target>
    local dir=$OUT-$1
    mkdir -p $dir
    cmake -S $SRC -B $dir -G "Unix Makefiles" \
        -DCMAKE_TOOLCHAIN_FILE=$SRC/Data/CMake/toolchain_mingw.cmake -DMINGW_TRIPLE=$2 \
        -DCMAKE_BUILD_TYPE=Release -DTUNE_CPU=none -DENABLE_LTO=False -DBUILD_TESTING=False \
        -DENABLE_JEMALLOC_GLIBC_ALLOC=False -DBUILD_FEXCONFIG=False -DENABLE_CCACHE=False \
        -DENABLE_OFFLINE_TELEMETRY=False -DHAS_CLANG_PRESERVE_ALL=0 > $dir/configure.log 2>&1
    grep -q 'FEX_HAS_PRESERVE_ALL_ATTR=0' $dir/CMakeCache.txt $dir/build.ninja $dir/Makefile 2>/dev/null ||
        grep -rq 'FEX_HAS_PRESERVE_ALL_ATTR=0' $dir/FEXCore 2>/dev/null ||
        { print "$1: preserve_all still on"; exit 1 }
    make -C $dir -j10 $3 > $dir/build.log 2>&1 || { tail -20 $dir/build.log; exit 1 }
    print "$1: $(ls $dir/Bin/lib$3.dll)"
}
build arm64ec arm64ec-w64-mingw32 arm64ecfex
build wow64 aarch64-w64-mingw32 wow64fex

mkdir -p $W/fex-dlls/aarch64-windows
ln -sf $OUT-arm64ec/Bin/libarm64ecfex.dll $W/fex-dlls/aarch64-windows/libarm64ecfex.dll
ln -sf $OUT-wow64/Bin/libwow64fex.dll $W/fex-dlls/aarch64-windows/libwow64fex.dll
print "fex-dlls updated"
