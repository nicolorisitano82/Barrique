#!/bin/zsh
# Round 19 diagnosis 2: why the builtin winemetal.dll is not found in WINEDLLPATH.
W=$HOME/Work/wine; T=$W/deps-dxmt/test; R=$W/tests/results/round19
export WINEPREFIX=$W/prefix-tests WINE_HVF=1 WINETEST_PLATFORM=wine
export WINEDLLOVERRIDES="d3d11,dxgi,d3d10core=n;mscoree=d;mshtml=d"
export WINEDEBUG=-all,err+all,+module,warn+seh WINEDLLPATH=$W/fex-dlls:$T/aarch64/dllpath
cd $W/build-arm64ec
timeout -k 10 40 ./wine $T/aarch64/app/gears-aarch64.exe d3d11 2 > $R/diag2-aarch64.log 2>&1
print "rc=$?"
timeout 30 ./server/wineserver -k
grep -a -i "winemetal" $R/diag2-aarch64.log | head -30
print DIAG2 DONE
