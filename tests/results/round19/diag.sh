#!/bin/zsh
# Round 19 diagnosis: why gears (aarch64) stops while loading DXMT's DLLs.
# Module loader traces only, one short run.
W=$HOME/Work/wine; T=$W/deps-dxmt/test; R=$W/tests/results/round19
export WINEPREFIX=$W/prefix-tests WINE_HVF=1 WINEDLLPATH=$W/fex-dlls:$T/unix WINETEST_PLATFORM=wine
export WINEDLLOVERRIDES="d3d11,dxgi,d3d10core,winemetal=n;mscoree=d;mshtml=d"
export WINEDEBUG=-all,err+all,warn+module,+loaddll,warn+seh DXMT_LOG_LEVEL=debug DXMT_LOG_PATH=$R
cd $W/build-arm64ec
timeout -k 10 40 ./wine $T/aarch64/gears-aarch64.exe d3d11 2 > $R/diag-aarch64.log 2>&1
print "rc=$?"
timeout 30 ./server/wineserver -k
grep -a "err:\|warn:module\|winemetal\|d3d11\|dxgi" $R/diag-aarch64.log | grep -av "^\s" | head -30
print DIAG DONE
