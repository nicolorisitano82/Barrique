#!/bin/zsh
# Phase 4 real programs under Barrique, in their own prefix (prefix-apps).
# Each step runs with a timeout; logs go to logs/<step>.log, one summary line per step.
set -u
W=$HOME/Work/wine
S=$W/samples/phase4
export WINEPREFIX=$W/prefix-apps WINE_HVF=1 WINEDLLPATH=$W/fex-dlls
export WINEDLLOVERRIDES="mscoree=d;mshtml=d" WINEDEBUG=${WINEDEBUG:--all}
WINE=$W/build-arm64ec/wine
C=$WINEPREFIX/drive_c
mkdir -p $S/logs
step() {   # step <name> <timeout> <command...>
    local name=$1 to=$2; shift 2
    local t0=$EPOCHREALTIME
    timeout -k 10 $to "$@" > $S/logs/$name.log 2>&1
    local rc=$? secs=$(( EPOCHREALTIME - t0 ))
    local res=$(grep -a "^RESULT:" $S/logs/$name.log | tail -1)
    printf "%-14s rc=%-4s %6.1fs  %s\n" $name $rc $secs "$res"
}
zmodload zsh/datetime
[[ -d $C ]] || step wineboot 300 $WINE wineboot -i
mkdir -p $C/probe $C/tmp
cp $S/src/probe.py $S/src/*.class $C/probe/
for a in 64 32; do
    [[ -d $C/py$a ]] || unzip -qo $S/python$a/*.zip -d $C/py$a
    step python$a 300 $WINE 'C:\py'$a'\python.exe' 'C:\probe\probe.py'
done
[[ -d $C/jre21 ]] || { unzip -qo $S/jre21-x64/jre21.zip -d $C/tmp/j21 && mv $C/tmp/j21/* $C/jre21; }
[[ -d $C/jre8 ]] || { unzip -qo $S/jre8-x86/jre8.zip -d $C/tmp/j8 && mv $C/tmp/j8/* $C/jre8; }
step java21-x64 300 $WINE 'C:\jre21\bin\java.exe' -cp 'C:\probe' Probe
for i in 1 2 3; do step java8-x86-$i 300 $WINE 'C:\jre8\bin\java.exe' -cp 'C:\probe' Probe; done
cp $S/7zip/*.msi $S/vcredist/*.exe $C/tmp/
rm -rf $C/tmp/7out $C/tmp/a.7z
step 7zip-msi-x64 300 $WINE msiexec /i 'C:\tmp\7z2301-x64.msi' /qn
step 7z-a-x64 300 $WINE 'C:\Program Files\7-Zip\7z.exe' a -mx=9 'C:\tmp\a.7z' 'C:\py64'
step 7z-x-x64 300 $WINE 'C:\Program Files\7-Zip\7z.exe' x -o'C:\tmp\7out' 'C:\tmp\a.7z'
diff -rq $C/py64 $C/tmp/7out/py64 > /dev/null && print "7z roundtrip x64: RESULT: ok" || print "7z roundtrip x64: RESULT: FAILED"
# the x86 package installs into the x64 one's folder if that is registered (7-Zip keeps its
# Path in the registry), so it gets a clean prefix of its own
(
    export WINEPREFIX=$W/prefix-apps32
    C32=$WINEPREFIX/drive_c
    [[ -d $C32 ]] || step wineboot32 300 $WINE wineboot -i
    mkdir -p $C32/tmp; cp $S/7zip/7z2301.msi $C32/tmp/
    [[ -d $C32/py32 ]] || unzip -qo $S/python32/*.zip -d $C32/py32
    step 7zip-msi-x86 300 $WINE msiexec /i 'C:\tmp\7z2301.msi' /qn
    rm -rf $C32/tmp/out $C32/tmp/b.7z
    step 7z-a-x86 300 $WINE 'C:\Program Files (x86)\7-Zip\7z.exe' a -mx=9 'C:\tmp\b.7z' 'C:\py32'
    step 7z-x-x86 300 $WINE 'C:\Program Files (x86)\7-Zip\7z.exe' x -o'C:\tmp\out' 'C:\tmp\b.7z'
    diff -rq $C32/py32 $C32/tmp/out/py32 > /dev/null && print "7z roundtrip x86: RESULT: ok" || print "7z roundtrip x86: RESULT: FAILED"
    $W/build-arm64ec/server/wineserver -k 2>/dev/null
)
step vcredist-x64 600 $WINE 'C:\tmp\vc_redist.x64.exe' /install /quiet /norestart
step vcredist-x86 600 $WINE 'C:\tmp\vc_redist.x86.exe' /install /quiet /norestart
ls -la $C/windows/system32/msvcp140.dll $C/windows/syswow64/msvcp140.dll 2>&1
$W/build-arm64ec/server/wineserver -k 2>/dev/null
print APPS DONE
