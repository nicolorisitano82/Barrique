#!/bin/zsh
# Round 10 (phase 5 step 3, video): run.sh base | after
# base = no GStreamer (winedmo demuxes with Homebrew FFmpeg, nothing decodes),
# after = our LGPL FFmpeg + GStreamer/gst-libav from build-arm64ec/deps.
# Media Foundation / DirectShow tests on three architectures, then mfplay.exe
# on every local test file compared with its ffmpeg reference (cmpref.py).
set -u
W=$HOME/Work/wine; export OUT=$W/tests/results/round10/${1:-after}
AV=$W/Barrique/tests/av; M=$W/samples/phase5/media
mkdir -p $OUT
for a in aarch64 x86_64 i386; do zsh $W/tests/run-conformance.sh $a mf mfplat mfreadwrite mfmediaengine quartz wmvcore; done
(
export WINEPREFIX=$W/prefix-tests WINE_HVF=1 WINEDLLPATH=$W/fex-dlls WINEDLLOVERRIDES="mscoree=d;mshtml=d" WINEDEBUG=-all
cd $W/build-arm64ec
for t in aarch64 x86_64 i686; do
    for f in $M/*.(mp4|webm|mp3|wmv|mpg|wav); do
        log=$OUT/mfplay-$t-${f:t:r}.log
        timeout -k 10 120 ./wine $W/samples/phase5/mfplay-$t.exe "Z:$f" > $log 2>&1
        rc=$?
        python3 $AV/cmpref.py $log $f.ref > $log.cmp
        print "mfplay $t ${f:t} rc=$rc $(tr '\n' ' ' < $log.cmp)"
    done
done
)
print ROUND10 ${1:-base} DONE
