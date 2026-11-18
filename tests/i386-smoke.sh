#!/bin/zsh
# x86 (32-bit) support is frozen (decision of 2026-11-18, research/drop-x86.md): no new
# 32-bit specific work, but what users rely on must keep working. This is the whole
# 32-bit check, run from Barrique.app with a new data folder:
#   - a 32-bit installer stub installing a 64-bit program (7-Zip x64, NSIS-style /S);
#   - a native 32-bit program (hello32.exe);
#   - a 32-bit game (OpenTTD win32, headless 20000 ticks);
#   - .NET x86 (tests/addons hello-x86.exe) and HTML through Gecko x86 (html-i686.exe).
#
#   i386-smoke.sh <data folder that does not exist yet>
W=$HOME/Work/Personale/wine
A=$W/dist/Barrique.app/Contents/MacOS/barrique
export BARRIQUE_DATA=${1:?data folder}
[[ -e $BARRIQUE_DATA ]] && { print "$BARRIQUE_DATA exists: use a new one"; exit 1 }
quiet() { grep -a -v -e '^\s*$' -e 'mvk-info' -e 'VK_KHR' -e 'Vulkan' -e '^	' }
bad=0
check() { if (( $1 )); then print "FAIL $2"; bad=1; else print "ok   $2"; fi }

timeout -k 5 300 $A --wine cmd /c echo ready 2>&1 | grep -q ready
check $? "prefix"

file -b $W/downloads/7zip/x64/7z2301-x64.exe | grep -q 'Intel 80386'
check $? "7-Zip x64 installer is a 32-bit stub"
timeout -k 5 300 $A --wine $W/downloads/7zip/x64/7z2301-x64.exe /S >/dev/null 2>&1
[[ -f "$BARRIQUE_DATA/prefix/drive_c/Program Files/7-Zip/7z.exe" ]]
check $? "32-bit installer installs 7-Zip x64"

timeout -k 5 120 $A --wine $W/hvproto/pe/hello32.exe 2>&1 | quiet | grep -q 'RESULT: ok'
check $? "hello32"

timeout -k 5 120 $A --wine $W/samples/openttd/win32/openttd-15.3-windows-win32/openttd.exe \
    -v null:ticks=20000 -s null -m null -g >/dev/null 2>&1
check $? "OpenTTD win32, 20000 ticks"

timeout -k 5 120 $A --wine $W/samples/addons/hello-x86.exe 2>&1 | quiet | grep -q 'X86, 32-bit, sum 166833'
check $? ".NET x86"

timeout -k 5 120 $A --wine $W/samples/addons/html-i686.exe 2>&1 | quiet | grep -q 'html: ok'
check $? "HTML (Gecko x86)"

WINEPREFIX=$BARRIQUE_DATA/prefix timeout 30 $W/dist/Barrique.app/Contents/Resources/wine/bin/wineserver -k
print "i386 smoke: $( (( bad )) && print FAILED || print ok)"
exit $bad
