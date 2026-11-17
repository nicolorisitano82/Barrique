#!/bin/zsh
# Round 11 check: Hiragino names in DirectWrite, Segoe UI Emoji replacement removed.
set -u
W=$HOME/Work/wine; R=$W/tests/results/round11; S=$W/samples/phase5
export WINEPREFIX=$W/prefix-tests WINE_HVF=1 WINEDLLPATH=$W/fex-dlls WINEDLLOVERRIDES="mscoree=d;mshtml=d" WINEDEBUG=-all
cd $W/build-arm64ec
timeout -k 10 60 ./wine reg delete 'HKCU\Software\Wine\Fonts\Replacements' /f > /dev/null 2>&1
timeout -k 10 60 ./server/wineserver -w
timeout -k 10 300 ./wine wineboot -u > $R/verify-wineboot.log 2>&1
timeout -k 10 60 ./server/wineserver -w
export OUT=$R/verify; mkdir -p $OUT
for a in aarch64 x86_64 i386; do zsh $W/tests/run-conformance.sh $a dwrite; done
for t in aarch64 x86_64 i686; do
    timeout -k 10 60 ./wine $S/cjkfont-$t.exe > $OUT/cjkfont-$t.log 2>&1
    timeout -k 10 60 ./wine $S/textshot-$t.exe "Z:$OUT/textshot-96-$t.bmp" > $OUT/textshot-96-$t.log 2>&1
    print "$t cjkfont $(grep -a '^RESULT' $OUT/cjkfont-$t.log) textshot $(grep -a -e '^dwrite MS' -e '^fallback' -e '^RESULT' $OUT/textshot-96-$t.log | tr '\n' ' ')"
done
timeout -k 10 60 ./server/wineserver -k
print VERIFY DONE
