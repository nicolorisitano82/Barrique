#!/bin/zsh
# Round 19: DXMT with ARM64X DLLs, one layout for both process kinds: the same
# winemetal.dll (ARM64X, builtin) in the prefix's system32 and in one WINEDLLPATH
# dir, d3d11/dxgi/d3d10core (ARM64X, native) next to the program. Metal window
# off: DXMT's buffers come from Windows memory (winemac cocoa_dxmt.m).
W=$HOME/Work/wine; T=$W/deps-dxmt/test-x; R=$W/tests/results/round19
export WINEPREFIX=$W/prefix-tests WINE_HVF=1 WINETEST_PLATFORM=wine WINEDLLPATH=$W/fex-dlls:$T/dllpath
export WINEDLLOVERRIDES="d3d11,dxgi,d3d10core=n;mscoree=d;mshtml=d"
export WINEDEBUG=-all,err+all,warn+seh DXMT_LOG_LEVEL=info DXMT_LOG_PATH=$R BARRIQUE_METAL_MEMORY=0
cd $W/build-arm64ec
for a in aarch64 x86_64; do
    print "== $a (ARM64X)"
    timeout -k 10 60 ./wine $T/$a/gears-$a.exe d3d11 3 > $R/gears-$a-arm64x.log 2>&1
    print "rc=$?"
    grep -a "^d3d11\|RESULT\|barrique:\|err:" $R/gears-$a-arm64x.log | grep -av "^\s\|winebth\|SvchostPush" | head -8
done
timeout 30 ./server/wineserver -k
print DXMT-X DONE
