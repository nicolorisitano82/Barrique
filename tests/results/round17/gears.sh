#!/bin/zsh
# Round 17: gears benchmark (tests/av/gears.c), OpenGL, D3D9, D3D11, D3D12,
# 5 s each at 640x480, vsync off, on aarch64, x86_64 and i686 under HVF.
W=$HOME/Work/wine; S=$W/samples/phase5; R=$W/tests/results/round17
export WINE_D3D_CONFIG=${WINE_D3D_CONFIG:-}
export WINEPREFIX=$W/prefix-tests WINE_HVF=1 WINEDLLPATH=$W/fex-dlls WINEDLLOVERRIDES="mscoree=d;mshtml=d" WINEDEBUG=-all,err+hvf WINETEST_PLATFORM=wine
cd $W/build-arm64ec
for a in aarch64 x86_64 i686; do
    print "== $a"
    timeout -k 10 150 ./wine $S/gears-$a.exe ${GEARS_API:-all} 5 > $R/gears-$a${GEARS_TAG:-}.log 2> $R/gears-$a${GEARS_TAG:-}.err
    print "rc=$?"
    cat $R/gears-$a${GEARS_TAG:-}.log
done
timeout 30 ./server/wineserver -k
print GEARS DONE
