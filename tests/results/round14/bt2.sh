#!/bin/zsh
# Round 14, Bluetooth listening check: the same quiet 440 Hz tone for 60 s
#  1. afplay (macOS, no Wine): reference for the Bluetooth link
#  2. Wine WASAPI aarch64, 48 kHz, one continuous stream (no API switches)
# Note the times of any dropout.
set -u
W=$HOME/Work/wine; R=$W/tests/results/round14; OUT=$R/bt
print "$(date +%T) afplay start"
afplay $OUT/tone-60s-48k.wav
print "$(date +%T) afplay end"
sleep 3
export WINEPREFIX=$W/prefix-tests WINE_HVF=1 WINEDLLPATH=$W/fex-dlls WINEDLLOVERRIDES="mscoree=d;mshtml=d" WINEDEBUG=-all
cd $W/build-arm64ec
print "$(date +%T) wine wasapi start"
timeout -k 10 120 ./wine $W/samples/phase5/tone-aarch64.exe wasapi 48000 60 10 > $OUT/tone60-wasapi.log 2>&1
print "$(date +%T) wine wasapi end rc=$? $(grep -a -e '^wasapi ' -e RESULT $OUT/tone60-wasapi.log | tr '\n' ' ')"
./server/wineserver -k
print BT2 DONE
