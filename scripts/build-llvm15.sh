#!/bin/zsh
# Build the LLVM 15 static libraries DXMT's shader compiler (airconv, linked into
# winemetal.so) needs: LLVM 15.0.7 exactly, arm64, macOS 15, no targets, no tools,
# with assertions as in DXMT's CI (phase 5 step 7).
#
#   build-llvm15.sh <download dir> [source dir] [prefix]
#
# <download dir> holds llvm-15.0.7.src.tar.xz and cmake-15.0.7.src.tar.xz
# (checked against SHA-256). Output: <prefix> (default ~/Work/wine/deps-llvm15)
# with include/, lib/*.a and lib/cmake/llvm, for meson -Dnative_llvm_path.
set -e
dl=${1:?usage: build-llvm15.sh <download dir> [source dir] [prefix]}
S=${2:-$HOME/Work/wine/deps-src/llvm-15.0.7}
P=${3:-$HOME/Work/wine/deps-llvm15}

typeset -A sums
sums=(
  llvm-15.0.7.src.tar.xz  4ad8b2cc8003c86d0078d15d987d84e3a739f24aae9033865c027abae93ee7a4
  cmake-15.0.7.src.tar.xz 8986f29b634fdaa9862eedda78513969fe9788301c9f2d938f4c10a3e7a3e7ea
)
for f in ${(k)sums}; do
    [[ $(shasum -a 256 $dl/$f | cut -d' ' -f1) == $sums[$f] ]] || { print "checksum mismatch: $f"; exit 1; }
done

# LLVM 15 standalone: llvm/ needs its sibling cmake/ (shared CMake modules)
if [[ ! -d $S/llvm ]]; then
    mkdir -p $S/llvm $S/cmake
    tar xJf $dl/llvm-15.0.7.src.tar.xz -C $S/llvm --strip-components 1
    tar xJf $dl/cmake-15.0.7.src.tar.xz -C $S/cmake --strip-components 1
fi

cmake -B $S/build -S $S/llvm -G Ninja \
    -DCMAKE_INSTALL_PREFIX=$P \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
    -DLLVM_HOST_TRIPLE=arm64-apple-darwin \
    -DLLVM_ENABLE_ASSERTIONS=On \
    -DLLVM_ENABLE_ZSTD=Off \
    -DLLVM_ENABLE_ZLIB=Off \
    -DLLVM_ENABLE_LIBXML2=Off \
    -DLLVM_ENABLE_TERMINFO=Off \
    -DLLVM_TARGETS_TO_BUILD="" \
    -DLLVM_BUILD_TOOLS=Off \
    -DLLVM_INCLUDE_TESTS=Off \
    -DLLVM_INCLUDE_BENCHMARKS=Off \
    -DLLVM_INCLUDE_EXAMPLES=Off \
    -DLLVM_VERSION_PRINTER_SHOW_HOST_TARGET_INFO=Off
cmake --build $S/build
cmake --install $S/build
du -sh $P
ls $P/lib/*.a | wc -l
