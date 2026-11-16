#!/bin/zsh
# Round 7 diagnostics: vcredist with/without bundled FreeType, then ntdll/kernel32 rerun.
set -u
W=$HOME/Work/wine; D=$W/tests/results/round7/diag
export WINEPREFIX=$W/prefix-apps WINE_HVF=1 WINEDLLPATH=$W/fex-dlls
export WINEDLLOVERRIDES="mscoree=d;mshtml=d" WINEDEBUG=-all
WINE=$W/build-arm64ec/wine
vc() { timeout -k 10 300 $WINE 'C:\tmp\vc_redist.x64.exe' /install /quiet /norestart > $D/$1.log 2>&1; print "$1 rc=$?"; }
mv $W/build-arm64ec/deps $W/build-arm64ec/deps.off
vc vc-nofreetype-1; vc vc-nofreetype-2
mv $W/build-arm64ec/deps.off $W/build-arm64ec/deps
vc vc-freetype-1; vc vc-freetype-2
for a in aarch64 x86_64 i386; do OUT=$D/nk zsh $W/tests/run-conformance.sh $a ntdll kernel32; done
print DIAG DONE
