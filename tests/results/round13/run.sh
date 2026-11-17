#!/bin/zsh
# Round 13 (phase 5 step 3): H.264 encoder through VideoToolbox (applemedia
# vtenc_h264 + h264parse), for the mf transform crash (h264enc: no encoder).
#  1. mf conformance (all units) on three architectures
#  2. mfplay.exe on every test file (aarch64, x86_64, i686): decoding must not
#     change (vtdec kept off)
set -u
W=$HOME/Work/wine; R=$W/tests/results/round13; S=$W/samples/phase5
AV=$W/Barrique/tests/av; M=$S/media
export WINE_HVF=1 WINEDLLPATH=$W/fex-dlls WINEDLLOVERRIDES="mscoree=d;mshtml=d" WINEDEBUG=-all WINETEST_PLATFORM=wine
export OUT=$R/after
mkdir -p $OUT
cd $W/build-arm64ec
for a in aarch64 x86_64 i386; do zsh $W/tests/run-conformance.sh $a mf; done
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
print ROUND13 DONE
