#!/bin/zsh
# Round 14, phase 5 step 3: VideoToolbox decoding (applemedia vtdec_hw) against
# FFmpeg software decoding (avdec_*), same build, chosen by
# GST_PLUGIN_FEATURE_RANK. mfplay.exe on 1080p 10 s H.264 and HEVC (300
# frames), aarch64 and x86_64: correctness against the reference, wall and
# CPU time (/usr/bin/time -l), decoder element from the GStreamer log.
set -u
W=$HOME/Work/wine; R=$W/tests/results/round14; S=$W/samples/phase5; P=$S/media-perf
AV=$W/Barrique/tests/av; OUT=$R/vt
mkdir -p $OUT
export WINEPREFIX=$W/prefix-tests WINE_HVF=1 WINEDLLPATH=$W/fex-dlls WINEDLLOVERRIDES="mscoree=d;mshtml=d" WINEDEBUG=-all
export GST_DEBUG="WINE:4"
cd $W/build-arm64ec
./wine wineboot > /dev/null 2>&1; ./server/wineserver -w   # warm up the prefix outside the timing
for mode in sw hw; do
    if [[ $mode == sw ]]; then export GST_PLUGIN_FEATURE_RANK="vtdec:NONE,vtdec_hw:NONE"
    else export GST_PLUGIN_FEATURE_RANK="vtdec_hw:257,vtdec:256"; fi
    for t in aarch64 x86_64; do for f in $P/*.mp4; do for n in 1 2; do
        base=$OUT/$mode-$t-${f:t:r}-$n
        /usr/bin/time -l timeout -k 10 300 ./wine $S/mfplay-$t.exe "Z:$f" > $base.log 2> $base.err
        rc=$?
        python3 $AV/cmpref.py $base.log $f.ref > $base.cmp
        dec=$(grep -a -o 'from factory <[a-z0-9_]*>' $base.err | sort -u | grep -o -e 'vtdec[a-z_]*' -e 'avdec_[a-z0-9]*' | sort -u | tr '\n' ' ')
        tm=$(grep ' real ' $base.err | tail -1 | tr -s ' ')
        print "$mode $t ${f:t} #$n rc=$rc dec=[$dec] $tm $(grep RESULT $base.cmp)"
    done; done; done
done
./server/wineserver -k
print VT DONE
