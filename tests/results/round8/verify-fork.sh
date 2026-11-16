#!/bin/zsh
# Round 8: forked registry save. tone WASAPI 44.1 kHz 20 s x2 per arch, audio
# tests (round8/fork) and rule 3 smoke test (round8/smoke) with real programs.
set -u
W=$HOME/Work/wine; R=$W/tests/results/round8
(
export WINEPREFIX=$W/prefix-tests WINE_HVF=1 WINEDLLPATH=$W/fex-dlls WINEDLLOVERRIDES="mscoree=d;mshtml=d" WINEDEBUG=-all
cd $W/build-arm64ec
mkdir -p $R/gap
for t in aarch64 x86_64 i686; do for i in 1 2; do
    timeout -k 10 90 ./wine $W/samples/phase5/tone-$t.exe wasapi 44100 20 10 > $R/gap/$t-fork-$i.log 2>&1
    print "tone $t#$i $(grep -a '^wasapi   open' $R/gap/$t-fork-$i.log)"
done; done
)
zsh $R/run.sh fork
mkdir -p $R/smoke
for a in aarch64 x86_64 i386; do OUT=$R/smoke zsh $W/tests/run-conformance.sh $a ntdll kernel32 user32 gdi32; done
cd $W/samples/phase4 && zsh run-apps.sh > $R/apps.out 2>&1
print VERIFY DONE
