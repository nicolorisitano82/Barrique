#!/bin/zsh
# Round 12 (phase 5 steps 1b and 3):
#  1. wineboot -u on prefix-tests (MPEG byte stream handler, MPEG video decoder
#     MFT, mp3dmod with MPEG-1 layer I/II input)
#  2. mfplay.exe on mpeg2.mpg (MPEG-2 video + MP2 audio in a program stream)
#     on three architectures, compared with its reference, plus the other
#     media as a regression check
#  3. mfsrcsnk mfplat mfreadwrite mp3dmod conformance on aarch64
#  4. dpitest.exe (unaware / system aware / per-monitor v2 windows) in
#     prefix-tests (96 DPI) and prefix-retina (Retina, 192 DPI), with a host
#     screenshot while the windows are up
set -u
W=$HOME/Work/wine; R=$W/tests/results/round12; S=$W/samples/phase5
AV=$W/Barrique/tests/av; M=$S/media
export WINE_HVF=1 WINEDLLPATH=$W/fex-dlls WINEDLLOVERRIDES="mscoree=d;mshtml=d" WINEDEBUG=-all WINETEST_PLATFORM=wine
export OUT=$R/after
mkdir -p $OUT
cd $W/build-arm64ec

WINEPREFIX=$W/prefix-tests timeout -k 10 300 ./wine wineboot -u > $R/wineboot-u.log 2>&1
WINEPREFIX=$W/prefix-tests timeout -k 10 60 ./server/wineserver -w
print "wineboot -u rc=$?"

for t in aarch64 x86_64 i686; do
    for f in $M/*.(mp4|webm|mp3|wmv|mpg|wav); do
        log=$OUT/mfplay-$t-${f:t:r}.log
        WINEPREFIX=$W/prefix-tests timeout -k 10 120 ./wine $S/mfplay-$t.exe "Z:$f" > $log 2>&1
        rc=$?
        python3 $AV/cmpref.py $log $f.ref > $log.cmp
        print "mfplay $t ${f:t} rc=$rc $(tr '\n' ' ' < $log.cmp)"
    done
done
WINEPREFIX=$W/prefix-tests timeout -k 10 60 ./server/wineserver -k

zsh $W/tests/run-conformance.sh aarch64 mfsrcsnk mfplat mfreadwrite mp3dmod

for p in tests retina; do
    for t in aarch64 x86_64 i686; do
        log=$OUT/dpitest-$p-$t.log
        WINEPREFIX=$W/prefix-$p timeout -k 10 60 ./wine $S/dpitest-$t.exe 8 "Z:$OUT/dpitest-$p-$t" > $log 2>&1 &
        sleep 6
        screencapture -x $OUT/screen-$p-$t.png 2>/dev/null
        wait
        print "dpitest $p $t: $(grep -a -e '^unaware' -e '^system' -e '^permonitor' -e '^RESULT' $log | tr '\n' ' ')"
    done
    WINEPREFIX=$W/prefix-$p timeout -k 10 60 ./server/wineserver -k
done
print ROUND12 DONE
