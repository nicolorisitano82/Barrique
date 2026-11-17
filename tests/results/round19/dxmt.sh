#!/bin/zsh
# Round 19: first DXMT run (main @ 0df8610, unmodified, wine_builtin_dll=false)
# under HVF, gears D3D11. d3d11/dxgi/d3d10core sit next to gears.exe as native DLLs
# (override n); winemetal.dll (builtin) and winemetal.so come from a per-arch
# WINEDLLPATH (deps-dxmt/test/<arch>/dllpath). For each of aarch64
# (ARM64 DLLs) and x86_64 (ARM64EC DLLs through FEX): once with hvf.c's window on
# Metal memory off (expected: guest fault on Metal buffer memory), once on
# (BARRIQUE_METAL_MEMORY=1). 64-bit only.
W=$HOME/Work/wine; T=$W/deps-dxmt/test; R=$W/tests/results/round19
export WINEPREFIX=$W/prefix-tests WINE_HVF=1 WINETEST_PLATFORM=wine
export WINEDLLOVERRIDES="d3d11,dxgi,d3d10core=n;mscoree=d;mshtml=d"
export WINEDEBUG=-all,err+all,warn+seh,+loaddll DXMT_LOG_LEVEL=info DXMT_LOG_PATH=$R
cd $W/build-arm64ec
# the prefix's system32 placeholder must match the process (ARM64 or ARM64EC)
# until winemetal.dll is built as ARM64X
for a in ${=DXMT_ARCHS:-aarch64 x86_64}; do
    [[ $a == aarch64 ]] && src=aarch64 || src=arm64ec
    cp $W/deps-dxmt/$src/winemetal.dll $WINEPREFIX/drive_c/windows/system32/winemetal.dll
    for m in ${=DXMT_WINDOW:-0 1}; do
        print "== $a metal-window=$m"
        WINEDLLPATH=$W/fex-dlls:$T/$a/dllpath BARRIQUE_METAL_MEMORY=$m \
            timeout -k 10 60 ./wine $T/$a/app/gears-$a.exe d3d11 3 > $R/gears-$a-mw$m.log 2>&1
        print "rc=$?"
        grep -a "^d3d11\|RESULT\|barrique:\|d3d11.dll\|err:" $R/gears-$a-mw$m.log | grep -av "^\s\|winebth\|SvchostPush" | head -10
        grep -ac "dispatch_exception\|handle_syscall_fault" $R/gears-$a-mw$m.log
    done
done
timeout 30 ./server/wineserver -k
print DXMT DONE
