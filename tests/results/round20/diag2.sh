#!/bin/zsh
# Round 20 diagnosis 2: where d3d11/d3d10core tests crash with DXMT (after a few
# seconds): exception traces (address, module, host faults with host backtrace).
W=$HOME/Work/wine; R=$W/tests/results/round20
export WINEPREFIX=$W/prefix-tests WINE_HVF=1 WINEDLLPATH=$W/fex-dlls WINETEST_PLATFORM=wine
export WINEDLLOVERRIDES="mscoree=d;mshtml=d" BARRIQUE_D3D=dxmt BARRIQUE_DXMT_DIR=$W/deps-dxmt/dxmt
export WINEDEBUG=-all,err+all,+seh,warn+module DXMT_LOG_LEVEL=info DXMT_LOG_PATH=none
cd $W/build-arm64ec
for m in d3d11 d3d10core; do
    timeout -k 10 60 ./wine dlls/$m/tests/aarch64-windows/${m}_test.exe $m > $R/diag2-$m.log 2>&1
    print "$m rc=$?"
    grep -an "dispatch_exception code=\|handle_syscall_fault\|host fault\|Unhandled\|backtrace" $R/diag2-$m.log | head -6 | cut -c1-220
    grep -an -A3 "dispatch_exception code=c" $R/diag2-$m.log | grep -a "pc=\|eip\|lr=" | head -3 | cut -c1-200
done
timeout 30 ./server/wineserver -k
print DIAG2 DONE
