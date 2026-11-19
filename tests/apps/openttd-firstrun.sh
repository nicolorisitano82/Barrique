#!/bin/zsh
# OpenTTD sometimes hangs on its first launch in a new prefix (later launches
# start in 3 s). Try up to N new prefixes; when a first launch is still running after
# 30 s, sample the hung openttd.exe host processes with macOS sample(1).
#
#   openttd-firstrun.sh <base dir for new data folders> [tries] [sample dir] [arch]
#   arch: win32 (default), win64 or arm64
W=$HOME/Work/Personale/wine
A=$W/dist/Barrique.app/Contents/MacOS/barrique
base=${1:?base dir}; tries=${2:-3}; out=${3:-/tmp}; arch=${4:-win32}
E=$W/samples/openttd/$arch/openttd-15.3-windows-$arch/openttd.exe
for i in {1..$tries}; do
    export BARRIQUE_DATA=$base-$i
    [[ -e $BARRIQUE_DATA ]] && { print "$BARRIQUE_DATA exists"; continue }
    timeout -k 5 300 $A --wine cmd /c echo ready >/dev/null 2>&1
    rcfile=$out/firstrun-rc-$i
    ( timeout -k 5 120 $A --wine $E -v null:ticks=20000 -s null -m null -g >/dev/null 2>&1; print $? > $rcfile ) &
    job=$!
    for s in {1..30}; do [[ -s $rcfile ]] && break; sleep 1; done
    if [[ -s $rcfile ]]; then
        print "try $i: finished, rc=$(<$rcfile)"
    else
        print "try $i: still running after 30 s"
        for p in $(pgrep -f "openttd-15.3-windows-$arch/openttd.exe"); do
            sample $p 3 -file $out/firstrun-sample-$i-$p.txt >/dev/null 2>&1 && print "  sampled pid $p"
        done
        ps -axo pid,stat,%cpu,command | grep -a 'wine\|openttd' | grep -v grep | head -15
    fi
    wait $job
    WINEPREFIX=$BARRIQUE_DATA/prefix timeout 30 $W/dist/Barrique.app/Contents/Resources/wine/bin/wineserver -k
done
