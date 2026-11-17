#!/bin/zsh
# Round 20 diagnosis: d3d11_test/dxgi_test crash in 1 s with BARRIQUE_D3D=dxmt
# (gears works). One run each on aarch64 with loader, error and exception
# messages and DXMT's own log.
W=$HOME/Work/wine; R=$W/tests/results/round20
export WINEPREFIX=$W/prefix-tests WINE_HVF=1 WINEDLLPATH=$W/fex-dlls WINETEST_PLATFORM=wine
export WINEDLLOVERRIDES="mscoree=d;mshtml=d" BARRIQUE_D3D=dxmt BARRIQUE_DXMT_DIR=$W/deps-dxmt/dxmt
export WINEDEBUG=-all,err+all,warn+seh,+loaddll,warn+module DXMT_LOG_LEVEL=info DXMT_LOG_PATH=$R
cd $W/build-arm64ec
for t in d3d11:d3d11 dxgi:dxgi; do
    m=${t%%:*} u=${t##*:}
    timeout -k 10 60 ./wine dlls/$m/tests/aarch64-windows/${m}_test.exe $u > $R/diag-$m.log 2>&1
    print "$m rc=$?"
    grep -av "^\s\|mvk-info\|trace:loaddll.*system32\\\\\\\\\(kernel\|ntdll\|ucrt\|msvcrt\|advapi\|sechost\|rpcrt\|user32\|gdi32\|win32u\)" $R/diag-$m.log | tail -12 | cut -c1-220
done
timeout 30 ./server/wineserver -k
print DIAG DONE
