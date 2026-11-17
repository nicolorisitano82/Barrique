#!/bin/zsh
# Round 13, msvproc with accurate_rnd on ARM64/ARM64EC: mf on three architectures.
set -u
W=$HOME/Work/wine; R=$W/tests/results/round13
export WINEPREFIX=$W/prefix-tests WINE_HVF=1 WINEDLLPATH=$W/fex-dlls WINEDLLOVERRIDES="mscoree=d;mshtml=d" WINEDEBUG=-all WINETEST_PLATFORM=wine
cd $W/build-arm64ec
export OUT=$R/after2
for a in aarch64 x86_64 i386; do zsh $W/tests/run-conformance.sh $a mf; done
timeout -k 10 60 ./server/wineserver -k
print DIAG3 DONE
