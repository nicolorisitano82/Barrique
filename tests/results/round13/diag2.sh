#!/bin/zsh
# Round 13, video processor NV12 -> RGB32: mf transform on aarch64 and i386
# with GStreamer logs (elements created, caps), to compare the pipelines.
set -u
W=$HOME/Work/wine; R=$W/tests/results/round13
export WINEPREFIX=$W/prefix-tests WINE_HVF=1 WINEDLLPATH=$W/fex-dlls WINEDLLOVERRIDES="mscoree=d;mshtml=d" WINEDEBUG=-all WINETEST_PLATFORM=wine
export GST_DEBUG="WINE:5,GST_ELEMENT_FACTORY:4,videoconvert:5"
export UNIT_TIMEOUT=300
cd $W/build-arm64ec
for a in aarch64 i386; do
    export OUT=$R/diag2-$a
    zsh $W/tests/run-conformance.sh $a mf
done
timeout -k 10 60 ./server/wineserver -k
print DIAG2 DONE
