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

# ARM64X: one DLL for ARM64 and x86-64 (ARM64EC) processes, as Wine's own builtins
# are; needed because the prefix has a single system32. Each DLL is relinked from
# both builds' objects with the arm64ec link command plus -marm64x.
if [[ -d $B/aarch64 && -d $B/arm64ec ]]; then
    mkdir -p $OUT/arm64x
    for dll in src/winemetal/winemetal.dll src/dxgi/dxgi.dll src/d3d11/d3d11.dll src/d3d10/d3d10core.dll; do
        cmd=$(cd $B/arm64ec && ninja -t commands $dll | tail -1)
        python3 - "$cmd" "$B" "$OUT/arm64x/${dll:t}" > $B/link-${dll:t:r}.sh <<'PY'
import shlex, sys
cmd, build, out = sys.argv[1], sys.argv[2], sys.argv[3]
args = shlex.split(cmd)
res, extra = [], []
i = 0
while i < len(args):
    a = args[i]
    if a == '-o':
        res += ['-o', out]; i += 2; continue
    res.append(a)
    # build-relative inputs: add the aarch64 build's file too
    # (resources such as version.o only once, from the arm64ec build)
    if not a.startswith(('-', '/', '.')) and (a.endswith('.obj') or a.endswith('.a')):
        extra.append(build + '/aarch64/' + a)
    i += 1
res.insert(1, '-marm64x')
# aarch64 objects right after the arm64ec ones, before the libraries
first_lib = next((k for k, a in enumerate(res) if a.startswith('-L') or a.startswith('-Wl')), len(res))
objs = [e for e in extra if e.endswith('.obj') or e.endswith('.o')]
libs = [e for e in extra if e.endswith('.a')]
res = res[:first_lib] + objs + res[first_lib:]
try:
    k = res.index('-Wl,--end-group')
    res = res[:k] + libs + res[k:]
except ValueError:
    res += libs
print('cd ' + shlex.quote(build + '/arm64ec') + ' && ' + ' '.join(shlex.quote(x) for x in res))
PY
        zsh $B/link-${dll:t:r}.sh
    done
    $WINE_BUILD/tools/winebuild/winebuild --builtin $OUT/arm64x/winemetal.dll
    cp $OUT/aarch64/winemetal.so $OUT/arm64x/
    ls -la $OUT/arm64x
fi
