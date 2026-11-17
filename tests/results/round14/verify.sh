#!/bin/zsh
# Round 14 verification (phase 5 step 3, VideoToolbox decoding on by default):
#  1. mfplay.exe on every test file and the 1080p ones, three architectures,
#     with the decoder GStreamer picked (h264-444.mp4: VideoToolbox cannot
#     decode it, gst-libav must take over)
#  2. mf mfplat mfreadwrite mfmediaengine quartz wmvcore on three architectures
set -u
W=$HOME/Work/wine; R=$W/tests/results/round14; S=$W/samples/phase5
AV=$W/Barrique/tests/av
export WINEPREFIX=$W/prefix-tests WINE_HVF=1 WINEDLLPATH=$W/fex-dlls WINEDLLOVERRIDES="mscoree=d;mshtml=d" WINEDEBUG=-all WINETEST_PLATFORM=wine
export OUT=$R/verify
mkdir -p $OUT
cd $W/build-arm64ec
for t in aarch64 x86_64 i686; do
    for f in $S/media/*.(mp4|webm|mp3|wmv|mpg|wav) $S/media-perf/*.mp4; do
        log=$OUT/mfplay-$t-${f:t:r}.log
        GST_DEBUG="WINE:4" timeout -k 10 300 ./wine $S/mfplay-$t.exe "Z:$f" > $log 2> $log.err
        rc=$?
        python3 $AV/cmpref.py $log $f.ref > $log.cmp
        dec=$(grep -a -o 'autoplug_select_cb: Using "[^"]*"' $log.err | grep -i -e decoder | sed 's/.*Using //' | sort -u | tr '\n' ' ')
        print "mfplay $t ${f:t} rc=$rc $(tr '\n' ' ' < $log.cmp) dec: $dec"
        gzip -f $log.err
    done
done
timeout -k 10 60 ./server/wineserver -k
for a in aarch64 x86_64 i386; do zsh $W/tests/run-conformance.sh $a mf mfplat mfreadwrite mfmediaengine quartz wmvcore; done
print VERIFY DONE
