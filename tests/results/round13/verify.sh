#!/bin/zsh
# Round 13 verification: H.264 encoder (vtenc_h264 + h264parse), MPEG audio
# decoder MFT on its own CLSID, accurate_rnd in msvproc and colorcnv on ARM.
# mf mfplat mfreadwrite mfmediaengine on three architectures, then mfplay.exe
# on every test file.
set -u
W=$HOME/Work/wine; R=$W/tests/results/round13; S=$W/samples/phase5
AV=$W/Barrique/tests/av; M=$S/media
export WINEPREFIX=$W/prefix-tests WINE_HVF=1 WINEDLLPATH=$W/fex-dlls WINEDLLOVERRIDES="mscoree=d;mshtml=d" WINEDEBUG=-all WINETEST_PLATFORM=wine
export OUT=$R/verify
mkdir -p $OUT
cd $W/build-arm64ec
for a in aarch64 x86_64 i386; do zsh $W/tests/run-conformance.sh $a mf mfplat mfreadwrite mfmediaengine; done
for t in aarch64 x86_64 i686; do
    for f in $M/*.(mp4|webm|mp3|wmv|mpg|wav); do
        log=$OUT/mfplay-$t-${f:t:r}.log
        timeout -k 10 120 ./wine $S/mfplay-$t.exe "Z:$f" > $log 2>&1
        rc=$?
        python3 $AV/cmpref.py $log $f.ref > $log.cmp
        print "mfplay $t ${f:t} rc=$rc $(tr '\n' ' ' < $log.cmp)"
    done
done
timeout -k 10 60 ./server/wineserver -k
print VERIFY DONE
