#!/bin/zsh
# Round 11 (phase 5 steps 1b and 3):
#  1. wineboot -u on prefix-tests (font replacements from wine.inf)
#  2. round 10 again (round10/after2): MPEG demuxer, WoW64 sample buffers
#  3. gdi32 user32 dwrite on three architectures (compare with round9/after),
#     cjkfont.exe and textshot.exe at 96 DPI
#  4. new prefix-retina with scripts/prefix-defaults.reg (RetinaMode, 192 DPI):
#     textshot.exe, plus gamma 1800 and FreeType interpreter 35 variants
set -u
W=$HOME/Work/wine; R=$W/tests/results/round11; S=$W/samples/phase5
export WINE_HVF=1 WINEDLLPATH=$W/fex-dlls WINEDLLOVERRIDES="mscoree=d;mshtml=d" WINEDEBUG=-all
cd $W/build-arm64ec

WINEPREFIX=$W/prefix-tests timeout -k 10 300 ./wine wineboot -u > $R/wineboot-u.log 2>&1
WINEPREFIX=$W/prefix-tests timeout -k 10 60 ./server/wineserver -w
print "wineboot -u rc=$?"

zsh $W/tests/results/round10/run.sh after2

export OUT=$R/after
mkdir -p $OUT
for a in aarch64 x86_64 i386; do zsh $W/tests/run-conformance.sh $a gdi32 user32 dwrite; done
for t in aarch64 x86_64 i686; do
    WINEPREFIX=$W/prefix-tests timeout -k 10 60 ./wine $S/cjkfont-$t.exe > $OUT/cjkfont-$t.log 2>&1
    print "cjkfont $t $(grep -a '^RESULT' $OUT/cjkfont-$t.log)"
    WINEPREFIX=$W/prefix-tests timeout -k 10 60 ./wine $S/textshot-$t.exe "Z:$OUT/textshot-96-$t.bmp" > $OUT/textshot-96-$t.log 2>&1
    print "textshot 96 $t $(grep -a '^RESULT' $OUT/textshot-96-$t.log)"
done
WINEPREFIX=$W/prefix-tests timeout -k 10 60 ./server/wineserver -k

P=$W/prefix-retina
if [[ ! -d $P ]]; then
    WINEPREFIX=$P timeout -k 10 300 ./wine wineboot -i > $R/retina-wineboot.log 2>&1
    WINEPREFIX=$P timeout -k 10 60 ./server/wineserver -w
    WINEPREFIX=$P timeout -k 10 60 ./wine regedit /S "Z:$W/Barrique/scripts/prefix-defaults.reg"
    WINEPREFIX=$P timeout -k 10 60 ./server/wineserver -w
fi
for t in aarch64 x86_64; do
    WINEPREFIX=$P timeout -k 10 60 ./wine $S/textshot-$t.exe "Z:$OUT/textshot-192-$t.bmp" > $OUT/textshot-192-$t.log 2>&1
    print "textshot 192 $t $(grep -a -e '^dpi' -e '^RESULT' $OUT/textshot-192-$t.log | tr '\n' ' ')"
done
WINEPREFIX=$P FREETYPE_PROPERTIES=truetype:interpreter-version=35 timeout -k 10 60 ./wine $S/textshot-aarch64.exe "Z:$OUT/textshot-192-v35.bmp" > $OUT/textshot-192-v35.log 2>&1
WINEPREFIX=$W/prefix-tests FREETYPE_PROPERTIES=truetype:interpreter-version=35 timeout -k 10 60 ./wine $S/textshot-aarch64.exe "Z:$OUT/textshot-96-v35.bmp" > $OUT/textshot-96-v35.log 2>&1
WINEPREFIX=$W/prefix-tests timeout -k 10 60 ./server/wineserver -k
WINEPREFIX=$P timeout -k 10 60 ./wine reg add "HKCU\\Control Panel\\Desktop" /v FontSmoothingGamma /t REG_DWORD /d 1800 /f > /dev/null 2>&1
WINEPREFIX=$P timeout -k 10 60 ./server/wineserver -w
WINEPREFIX=$P timeout -k 10 60 ./wine $S/textshot-aarch64.exe "Z:$OUT/textshot-192-g1800.bmp" > $OUT/textshot-192-g1800.log 2>&1
WINEPREFIX=$P timeout -k 10 60 ./wine reg delete "HKCU\\Control Panel\\Desktop" /v FontSmoothingGamma /f > /dev/null 2>&1
WINEPREFIX=$P timeout -k 10 60 ./server/wineserver -k
print ROUND11 DONE
