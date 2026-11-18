#!/bin/zsh
# Wine Mono and Gecko from Barrique.app, with a new data folder (prefix creation
# included): build tests/addons/hello.cs with Mono's mcs (AnyCPU, x86, x64), run
# the three builds, then the mshtml check (html.c) on x86_64, i686 and aarch64
# (no Gecko exists for ARM64: expected to fail there).
#
#   run.sh <data folder that does not exist yet>
W=$HOME/Work/wine
A=$W/dist/Barrique.app/Contents/MacOS/barrique
MCS=$W/dist/Barrique.app/Contents/Resources/wine/share/wine/mono/wine-mono-11.3.0/lib/mono/4.5/mcs.exe
O=$W/samples/addons
T=${0:A:h}
export BARRIQUE_DATA=${1:?data folder}
[[ -e $BARRIQUE_DATA ]] && { print "$BARRIQUE_DATA exists: use a new one"; exit 1 }

winpath() { print -r -- "Z:${1//\//\\}" }
quiet() { grep -a -v -e '^\s*$' -e 'mvk-info' -e 'VK_KHR' -e 'Vulkan' -e '^	' }

start=$SECONDS
timeout -k 5 300 $A --wine cmd /c echo prefix-ready 2>&1 | grep -a prefix-ready
print "prefix: $((SECONDS - start)) s"
for p in anycpu x86 x64; do
    timeout -k 5 300 $A --wine $MCS -nologo -platform:$p -out:$(winpath $O/hello-$p.exe) $(winpath $T/hello.cs) 2>&1 | quiet | tail -2
    print "built $p: $([[ -f $O/hello-$p.exe ]] && print yes || print no)"
done
for p in anycpu x86 x64; do
    print "run $p: $(timeout -k 5 120 $A --wine $O/hello-$p.exe 2>&1 | quiet | grep -a dotnet)"
done
for a in x86_64 i686 aarch64; do
    print "html $a: $(timeout -k 5 120 $A --wine $O/html-$a.exe 2>&1 | quiet | grep -a 'html:')"
done
WINEPREFIX=$BARRIQUE_DATA/prefix timeout 30 $W/dist/Barrique.app/Contents/Resources/wine/bin/wineserver -k
