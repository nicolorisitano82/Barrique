#!/bin/zsh
# Round 9: CJK font replacements (win32u/font.c) and x64 breakpoints under
# FEX ARM64EC (ntdll/unix/thread.c). cjkfont.exe on three architectures, then
# kernel32 gdi32 user32 dwrite (compare with round8/smoke and round7/after).
set -u
W=$HOME/Work/wine; R=$W/tests/results/round9; export OUT=$R/after
mkdir -p $OUT
(
export WINEPREFIX=$W/prefix-tests WINE_HVF=1 WINEDLLPATH=$W/fex-dlls WINEDLLOVERRIDES="mscoree=d;mshtml=d" WINEDEBUG=-all
cd $W/build-arm64ec
for t in aarch64 x86_64 i686; do
    timeout -k 10 60 ./wine $W/samples/phase5/cjkfont-$t.exe > $OUT/cjkfont-$t.log 2>&1
    print "cjkfont $t rc=$? $(grep -a '^RESULT' $OUT/cjkfont-$t.log)"
done
)
for a in aarch64 x86_64 i386; do zsh $W/tests/run-conformance.sh $a kernel32 gdi32 user32 dwrite; done
print ROUND9 DONE
