#!/bin/zsh
# Round 13 diagnostics: wineboot -u (MPEG audio decoder MFT registered on its
# own CLSID), then mf transform on each architecture, keeping the bitmaps the
# test dumps to %TEMP% (video processor NV12 -> RGB32 output) per architecture.
set -u
W=$HOME/Work/wine; R=$W/tests/results/round13
export WINEPREFIX=$W/prefix-tests WINE_HVF=1 WINEDLLPATH=$W/fex-dlls WINEDLLOVERRIDES="mscoree=d;mshtml=d" WINEDEBUG=-all WINETEST_PLATFORM=wine
T=$WINEPREFIX/drive_c/users/$USER/AppData/Local/Temp
cd $W/build-arm64ec
timeout -k 10 300 ./wine wineboot -u > $R/wineboot-u.log 2>&1
timeout -k 10 60 ./server/wineserver -w
print "wineboot -u rc=$?"
for a in aarch64 x86_64 i386; do
    export OUT=$R/diag-$a
    mkdir -p $OUT/dump
    zsh $W/tests/run-conformance.sh $a mf
    cp $T/rgb32frame*.bmp $T/nv12frame*.bmp $OUT/dump/ 2>/dev/null
done
timeout -k 10 60 ./server/wineserver -k
print DIAG DONE
