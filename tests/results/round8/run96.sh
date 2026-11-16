#!/bin/zsh
# Round 8: output device at 96 kHz. tone.exe all APIs at 44.1/48/96 kHz,
# 20 s, 10 ms period, three architectures (round8/r96), then the audio
# conformance tests (round8/r96), to compare with round8/fork (48 kHz device).
set -u
W=$HOME/Work/wine; R=$W/tests/results/round8; OUT=$R/r96
mkdir -p $OUT
(
export WINEPREFIX=$W/prefix-tests WINE_HVF=1 WINEDLLPATH=$W/fex-dlls WINEDLLOVERRIDES="mscoree=d;mshtml=d" WINEDEBUG=-all
cd $W/build-arm64ec
for t in aarch64 x86_64 i686; do for r in 44100 48000 96000; do
    timeout -k 10 120 ./wine $W/samples/phase5/tone-$t.exe all $r 20 10 > $OUT/tone-$t-$r.log 2>&1
    print "tone $t $r rc=$? $(grep -a '^RESULT' $OUT/tone-$t-$r.log)"
done; done
)
export OUT
for a in aarch64 x86_64 i386; do zsh $W/tests/run-conformance.sh $a mmdevapi dsound winmm xaudio2_7; done
print R96 DONE
