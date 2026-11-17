#!/bin/zsh
# Barrique.app/Contents/MacOS/barrique: runs Windows programs with the Wine in
# the bundle. Everything that changes lives in one folder,
# ~/Library/Application Support/Barrique (prefix, logs); the bundle stays as signed.
#
#   barrique [program.exe [args...]]     no program: Wine's file manager
#   barrique --wine <wine command...>    any Wine command (winecfg, regedit, wineboot...)
#
# Settings (environment, read once per run):
#   BARRIQUE_D3D=wined3d|dxmt   Direct3D 10/11 backend (default wined3d until the
#                               phase 5 step 6 bench says otherwise)
#   BARRIQUE_PREFIX=<dir>       another prefix (default <data>/prefix)
set -e
APP=${0:A:h:h}
R=$APP/Resources/wine
DATA=${BARRIQUE_DATA:-$HOME/Library/Application Support/Barrique}
export WINEPREFIX=${BARRIQUE_PREFIX:-$DATA/prefix}
export WINE_HVF=1                                   # Windows code runs in the Hypervisor.framework VM
export WINEDLLOVERRIDES=${WINEDLLOVERRIDES:-"mscoree=d;mshtml=d"}   # no Mono/Gecko download prompts (not bundled yet)
export BARRIQUE_D3D=${BARRIQUE_D3D:-wined3d}
export WINEDEBUG=${WINEDEBUG:--all}
mkdir -p "$DATA/logs"
log="$DATA/logs/barrique.log"
wine=$R/bin/wine

if [[ ! -f $WINEPREFIX/system.reg ]]; then
    print "$(date '+%F %T') creating prefix $WINEPREFIX" >> $log
    mkdir -p "$WINEPREFIX"
    "$wine" wineboot -i >> $log 2>&1
    reg=$(mktemp -t barrique).reg
    cat $R/share/barrique/prefix-defaults.reg > $reg
    cat >> $reg <<'EOF'

; x86 code: FEX through WoW64 (32-bit) and ARM64EC (64-bit)
[HKEY_LOCAL_MACHINE\Software\Microsoft\Wow64\amd64]
@="libarm64ecfex.dll"

[HKEY_LOCAL_MACHINE\Software\Microsoft\Wow64\x86]
@="libwow64fex.dll"
EOF
    "$wine" regedit /S "$reg" >> $log 2>&1
    rm -f "$reg"
    # DXMT's winemetal.dll is not a Wine DLL: its system32 placeholder makes the
    # loader look for the builtin (lib/wine/dxmt, BARRIQUE_D3D=dxmt)
    cp $R/lib/wine/dxmt/aarch64-windows/winemetal.dll "$WINEPREFIX/drive_c/windows/system32/"
    "$R/bin/wineserver" -w
fi

if [[ $1 == --wine ]]; then
    shift
    exec "$wine" "$@"
elif [[ -n $1 ]]; then
    exec "$wine" start /unix "$@"
else
    exec "$wine" winefile
fi
