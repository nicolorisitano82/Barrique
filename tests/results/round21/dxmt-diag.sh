#!/bin/zsh
# Round 21 diagnosis: gears D3D11 with BARRIQUE_D3D=dxmt fails to start from the
# bundle (rc 66). Loader messages, aarch64 only, same test data folder.
W=$HOME/Work/wine; A=$W/dist/Barrique.app/Contents/MacOS/barrique; S=$W/samples/phase5; R=$W/tests/results/round21
export BARRIQUE_DATA=$W/dist/test-data BARRIQUE_D3D=dxmt WINEDEBUG=-all,err+all,warn+module,+loaddll,trace+module
timeout -k 10 60 $A --wine $S/gears-aarch64.exe d3d11 2 > $R/dxmt-diag.log 2>&1
print "rc=$?"
grep -a -i "dxmt\|winemetal\|d3d11\|dxgi\|err:" $R/dxmt-diag.log | grep -av "^\s" | head -25 | cut -c1-230
timeout 30 $W/dist/Barrique.app/Contents/Resources/wine/bin/wineserver -k 2>/dev/null
print DIAG DONE
