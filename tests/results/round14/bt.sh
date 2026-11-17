#!/bin/zsh
# Round 14, phase 5 step 2: Bluetooth output (AirPods, 48 kHz A2DP).
#  1. tone.exe all APIs at 48 and 44.1 kHz, 10 s per API, 10 ms period, three
#     architectures (listen: about 4 minutes of a quiet tone)
#  2. audio conformance on three architectures; the capture tests open the
#     AirPods microphone, which switches them to the hands-free profile (HFP)
set -u
W=$HOME/Work/wine; R=$W/tests/results/round14; OUT=$R/bt
mkdir -p $OUT
system_profiler SPAudioDataType > $OUT/devices-before.txt 2>&1
(
export WINEPREFIX=$W/prefix-tests WINE_HVF=1 WINEDLLPATH=$W/fex-dlls WINEDLLOVERRIDES="mscoree=d;mshtml=d" WINEDEBUG=-all
cd $W/build-arm64ec
for t in aarch64 x86_64 i686; do for r in 48000 44100; do
    print "$(date +%T) playing: tone $t $r"
    timeout -k 10 120 ./wine $W/samples/phase5/tone-$t.exe all $r 10 10 > $OUT/tone-$t-$r.log 2>&1
    print "tone $t $r rc=$? $(grep -a '^RESULT' $OUT/tone-$t-$r.log)"
done; done
timeout -k 10 60 ./server/wineserver -k
)
print "$(date +%T) listening part done"
export OUT
for a in aarch64 x86_64 i386; do zsh $W/tests/run-conformance.sh $a mmdevapi dsound winmm xaudio2_7; done
system_profiler SPAudioDataType > $OUT/devices-after.txt 2>&1
print BT DONE
