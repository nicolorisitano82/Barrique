#!/bin/zsh
# Round 21 (phase 6): first run of dist/Barrique.app. Data folder for the test:
# ~/Work/wine/dist/test-data (not ~/Library yet). The launcher creates the prefix
# (wineboot, Retina/font defaults, FEX registry keys, winemetal placeholder),
# then: cmd echo, gears with every API on aarch64/x86_64/i686, gears D3D11 with
# BARRIQUE_D3D=dxmt on aarch64/x86_64. Only the bundle is used (no build tree).
W=$HOME/Work/wine; A=$W/dist/Barrique.app/Contents/MacOS/barrique; S=$W/samples/phase5; R=$W/tests/results/round21
export BARRIQUE_DATA=$W/dist/test-data
rm -rf $BARRIQUE_DATA
time (timeout -k 10 400 $A --wine cmd /c echo barrique-ok > $R/echo.log 2>&1); print "echo rc=$? $(tr -d '\r' < $R/echo.log | grep -a barrique-ok)"
ls $BARRIQUE_DATA/prefix/drive_c/windows/system32 | wc -l
grep -a "Wow64\\\\\\\\amd64\|Wow64\\\\\\\\x86" -A2 $BARRIQUE_DATA/prefix/system.reg | grep -a "@=" 
for a in aarch64 x86_64 i686; do
    timeout -k 10 120 $A --wine $S/gears-$a.exe all 3 > $R/gears-$a.log 2>&1
    print "gears $a rc=$?"; grep -a "^gl\|^d3d\|RESULT" $R/gears-$a.log
done
for a in aarch64 x86_64; do
    BARRIQUE_D3D=dxmt timeout -k 10 60 $A --wine $S/gears-$a.exe d3d11 3 > $R/gears-$a-dxmt.log 2>&1
    print "gears $a dxmt rc=$?"; grep -a "^d3d11\|barrique:" $R/gears-$a-dxmt.log
done
timeout 30 $W/dist/Barrique.app/Contents/Resources/wine/bin/wineserver -k 2>/dev/null
tail -5 $BARRIQUE_DATA/logs/barrique.log
print APP DONE
