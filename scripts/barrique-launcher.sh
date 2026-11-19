#!/bin/zsh
# Barrique.app/Contents/Resources/barrique.sh: runs Windows programs with the Wine in
# the bundle. Started by the native launcher (Contents/MacOS/barrique, launcher/main.m)
# for files opened in the Finder, a double click on the app, and terminal use.
# Everything that changes lives in one folder, ~/Library/Application Support/Barrique
# (prefix, logs); the bundle stays as signed.
#
#   barrique [program.exe [args...]]     no program: Wine's file manager
#   barrique --wine <wine command...>    any Wine command (winecfg, regedit, wineboot...)
#
# Settings (environment, read once per run):
#   BARRIQUE_D3D=dxmt|wined3d   Direct3D 10/11 backend (default dxmt: over Apple's
#                               GL 2.1 wined3d only reaches level 9_3, tests/av/stress3d;
#                               32-bit programs have no DXMT and use wined3d anyway)
#   BARRIQUE_PREFIX=<dir>       another prefix (default <data>/prefix)
#   BARRIQUE_DATA=<dir>         another data folder (default ~/Library/Application Support/Barrique)
set -e
APP=${0:A:h:h}
R=$APP/Resources/wine
DATA=${BARRIQUE_DATA:-$HOME/Library/Application Support/Barrique}
export WINEPREFIX=${BARRIQUE_PREFIX:-$DATA/prefix}
export WINE_HVF=1                                   # Windows code runs in the Hypervisor.framework VM
export BARRIQUE_D3D=${BARRIQUE_D3D:-dxmt}
export WINEDEBUG=${WINEDEBUG:--all}
mkdir -p "$DATA/logs"
log="$DATA/logs/barrique.log"
wine=$R/bin/wine
# prefix-defaults.reg is applied again when it changes (new font replacements,
# settings), so that data folders made by an older Barrique get them too
defaults=$R/share/barrique/prefix-defaults.reg
stamp=$WINEPREFIX/.barrique-defaults
sum=$(shasum $defaults | cut -c1-40)

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
    print $sum > $stamp
fi
if [[ ! -f $stamp || $(<$stamp) != $sum ]]; then
    print "$(date '+%F %T') applying new prefix defaults" >> $log
    # a failure here must not stop the program from starting; retried next run
    if "$wine" regedit /S "$defaults" >> $log 2>&1; then
        "$R/bin/wineserver" -w
        print $sum > $stamp
    else
        print "$(date '+%F %T') prefix defaults not applied, retrying next run" >> $log
    fi
fi

if [[ $1 == --wine ]]; then
    shift
    exec "$wine" "$@"
elif [[ -n $1 ]]; then
    exec "$wine" start /unix "$@"
else
    exec "$wine" winefile
fi
