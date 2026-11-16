#!/bin/zsh
# Round 8 (phase 5 step 2, audio): run.sh <mode>
# Conformance tests of the audio modules on three architectures, then tone.exe
# (WASAPI, waveOut, DirectSound, XAudio2) at 44.1 and 48 kHz, 10 ms period.
set -u
W=$HOME/Work/wine
export OUT=$W/tests/results/round8/${1:-base}
mkdir -p $OUT
for a in aarch64 x86_64 i386; do zsh $W/tests/run-conformance.sh $a mmdevapi dsound winmm xaudio2_7; done

export WINEPREFIX=$W/prefix-tests WINE_HVF=1 WINEDLLPATH=$W/fex-dlls
export WINEDLLOVERRIDES="mscoree=d;mshtml=d" WINEDEBUG=-all
cd $W/build-arm64ec
for t in aarch64 x86_64 i686; do
    for r in 44100 48000; do
        timeout -k 10 120 ./wine $W/samples/phase5/tone-$t.exe all $r 3 10 > $OUT/tone-$t-$r.log 2>&1
        print "tone $t $r rc=$? $(grep -a '^RESULT' $OUT/tone-$t-$r.log)"
    done
done
print ROUND8 ${1:-base} DONE
