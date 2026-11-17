#!/bin/zsh
# Round 12 diagnostics:
#  1. mfplay.exe on mpeg2.mpg (aarch64) with Media Foundation traces
#  2. dpitest.exe in prefix-retina (aarch64) with the macOS window bounds in
#     points (winbounds), to see whether non-DPI-aware windows are scaled
set -u
W=$HOME/Work/wine; R=$W/tests/results/round12; S=$W/samples/phase5
export WINE_HVF=1 WINEDLLPATH=$W/fex-dlls WINEDLLOVERRIDES="mscoree=d;mshtml=d" WINETEST_PLATFORM=wine
mkdir -p $R/diag
cd $W/build-arm64ec
WINEPREFIX=$W/prefix-tests WINEDEBUG=+mfplat,+dmo,+mfreadwrite,+mfsrcsnk timeout -k 10 120 ./wine $S/mfplay-aarch64.exe "Z:$S/media/mpeg2.mpg" > $R/diag/mpeg2-trace.log 2>&1
print "mfplay mpeg2 rc=$? $(grep -a -c . $R/diag/mpeg2-trace.log) lines"
WINEPREFIX=$W/prefix-tests timeout -k 10 60 ./server/wineserver -k

WINEPREFIX=$W/prefix-retina WINEDEBUG=-all timeout -k 10 60 ./wine $S/dpitest-aarch64.exe 8 > $R/diag/dpitest-retina.log 2>&1 &
sleep 5
{ $S/winbounds wine; $S/winbounds dpitest; } > $R/diag/winbounds-retina.txt
wait
WINEPREFIX=$W/prefix-retina timeout -k 10 60 ./server/wineserver -k
print "winbounds:"; cat $R/diag/winbounds-retina.txt
print DIAG DONE
