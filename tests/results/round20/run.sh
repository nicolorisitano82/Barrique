#!/bin/zsh
# Round 20 (phase 5 step 7): DXMT through the BARRIQUE_D3D=dxmt switch (ntdll
# loads d3d11/dxgi/d3d10core/winemetal from BARRIQUE_DXMT_DIR before Wine's own;
# no overrides, no DLLs next to the programs).
# 1. gears D3D11 on aarch64, x86_64 and i686 (i686: no i386 DXMT, so wined3d)
# 2. d3d11, dxgi, d3d10core conformance on aarch64, x86_64 (and i386, wined3d)
#    with DXMT; compare with round16/vk2 and round15 (wined3d).
W=$HOME/Work/wine; S=$W/samples/phase5; R=$W/tests/results/round20
export WINEPREFIX=$W/prefix-tests WINE_HVF=1 WINEDLLPATH=$W/fex-dlls WINETEST_PLATFORM=wine
export WINEDLLOVERRIDES="mscoree=d;mshtml=d" BARRIQUE_D3D=dxmt BARRIQUE_DXMT_DIR=$W/deps-dxmt/dxmt
export WINEDEBUG=-all,err+hvf DXMT_LOG_LEVEL=error DXMT_LOG_PATH=none
cd $W/build-arm64ec
for a in aarch64 x86_64 i686; do
    timeout -k 10 60 ./wine $S/gears-$a.exe d3d11 3 > $R/gears-$a.log 2>&1
    print "gears $a rc=$? $(grep -a '^d3d11\|barrique:' $R/gears-$a.log | tr '\n' ' ')"
done
timeout 30 ./server/wineserver -k
export OUT=$R/conf
for a in aarch64 x86_64 i386; do zsh $W/tests/run-conformance.sh $a d3d11 dxgi d3d10core; done
print ROUND20 DONE
