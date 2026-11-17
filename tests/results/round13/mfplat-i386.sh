#!/bin/zsh
# Round 13: mfplat on i386 twice (a child "mfplat u1" hung once in verify).
set -u
W=$HOME/Work/wine; R=$W/tests/results/round13
export WINEPREFIX=$W/prefix-tests WINE_HVF=1 WINEDLLPATH=$W/fex-dlls WINEDLLOVERRIDES="mscoree=d;mshtml=d" WINEDEBUG=-all WINETEST_PLATFORM=wine
cd $W/build-arm64ec
for n in 1 2; do
    export OUT=$R/mfplat-i386-$n
    zsh $W/tests/run-conformance.sh i386 mfplat
done
timeout -k 10 60 ./server/wineserver -k
print MFPLAT-I386 DONE
