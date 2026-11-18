#!/bin/zsh
# OpenTTD 15.3 (GPL, official builds for ARM64, x86 and x64 from cdn.openttd.org,
# with OpenGFX 8.0) as a test game, run from Barrique.app.
#
#   openttd.sh headless [ticks] [runs]   null video/sound/music: generate a map, run
#                                         the ticks, exit; per architecture, timed
#   openttd.sh play <arm64|win32|win64>  the game with its window (OpenGL), by hand
#
# Expects the zips extracted under ~/Work/wine/samples/openttd/<arch>/ with OpenGFX
# in each baseset/ folder. BARRIQUE_DATA picks the data folder (default: a test one).
zmodload zsh/datetime
W=$HOME/Work/wine
A=$W/dist/Barrique.app/Contents/MacOS/barrique
S=$W/samples/openttd
export BARRIQUE_DATA=${BARRIQUE_DATA:-$W/dist/test-addons-1}
exe() { print $S/$1/openttd-15.3-windows-$1/openttd.exe }
stop() { WINEPREFIX=$BARRIQUE_DATA/prefix timeout 30 $W/dist/Barrique.app/Contents/Resources/wine/bin/wineserver -k }

case ${1:-headless} in
headless)
    ticks=${2:-50000}; runs=${3:-1}; bad=0
    for a in arm64 win64 win32; do
        for i in {1..$runs}; do
            st=$EPOCHREALTIME
            timeout -k 5 120 $A --wine $(exe $a) -v null:ticks=$ticks -s null -m null -g >/dev/null 2>&1
            rc=$?
            printf "%s ticks=%s rc=%d %.1f s\n" $a $ticks $rc $(( EPOCHREALTIME - st ))
            (( rc )) && bad=1
            stop
        done
    done
    print "openttd: $( (( bad )) && print FAILED || print ok)"
    exit $bad
    ;;
play)
    exec $A $(exe ${2:?arch: arm64, win32 or win64})
    ;;
esac
