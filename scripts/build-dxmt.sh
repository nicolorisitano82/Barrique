#!/bin/zsh
# Build DXMT (D3D10/11 on Metal, LGPL-2.1) unmodified against Barrique's Wine:
# PE DLLs for aarch64 (native ARM64 programs) and arm64ec (x86-64 programs
# through FEX), each with winemetal.so (arm64, Metal, airconv with LLVM 15).
# DXMT forbids AI-generated contributions: its sources are never edited here,
# the cross files live in the build directories (phase 5 step 7).
# wine_builtin_dll=false: d3d11, dxgi, d3d10core are plain native DLLs (a builtin
# one is replaced by Wine's own); winemetal.dll stays builtin for its unix lib.
#
#   build-dxmt.sh [source dir] [wine build dir] [llvm prefix] [output dir] [archs]
#
# Output: <output dir>/<arch>/ with the DLLs and winemetal.so, default
# ~/Work/wine/deps-dxmt; archs default "aarch64 arm64ec".
set -e
S=${1:-$HOME/Work/wine/deps-src/dxmt-0df8610}   # main @ 0df8610 (2026-11-10): v0.80 does not build with Xcode 27
WINE_BUILD=${2:-$HOME/Work/wine/build-arm64ec}
LLVM=${3:-$HOME/Work/wine/deps-llvm15}
OUT=${4:-$HOME/Work/wine/deps-dxmt}
archs=(${=${5:-aarch64 arm64ec}})
MINGW=$HOME/Work/wine/llvm-mingw/bin
B=$HOME/Work/wine/deps-src/dxmt-build

export MACOSX_DEPLOYMENT_TARGET=15.0
unset CFLAGS CXXFLAGS CPPFLAGS LDFLAGS   # nothing from the user's shell (Homebrew paths)
mkdir -p $B $OUT
for arch in $archs; do
    cross=$B/cross-$arch.txt
    cat > $cross <<EOF
[binaries]
c = '$MINGW/$arch-w64-mingw32-clang'
cpp = '$MINGW/$arch-w64-mingw32-clang++'
ar = '$MINGW/$arch-w64-mingw32-ar'
strip = '$MINGW/$arch-w64-mingw32-strip'
windres = '$MINGW/$arch-w64-mingw32-windres'

[properties]
needs_exe_wrapper = true

[host_machine]
system = 'windows'
cpu_family = 'aarch64'
cpu = 'aarch64'
endian = 'little'
EOF
    rm -rf $B/$arch
    # newer llvm-mingw libc++ no longer pulls in <functional> through <string>;
    # DXMT 0.80 relies on it (std::equal_to in sha1_util.hpp), so force it here
    meson setup --cross-file $cross -Dnative_llvm_path=$LLVM -Dwine_build_path=$WINE_BUILD \
        -Dcpp_args="-include functional" -Dwine_builtin_dll=false --buildtype release $B/$arch $S
    meson compile -C $B/$arch
    mkdir -p $OUT/$arch
    find $B/$arch/src -name "*.dll" -o -name "winemetal.so" | while read f; do cp "$f" $OUT/$arch/; done
    ls -la $OUT/$arch
done
