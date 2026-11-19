#!/bin/zsh
# Barrique stress3d: builds stress3d.c for ARM64, x86-64 and x86, runs every API
# from Barrique.app on each (Direct3D 11 twice: wined3d and DXMT, not for x86 where
# DXMT is not available), appends the metrics to a JSON lines file and prints a table.
#
#   run.sh [seconds] [instances] [output.jsonl]     default 8 s, 4096 cubes,
#                                                    tests/results/stress3d-<date>.jsonl
W=$HOME/Work/Personale/wine
T=${0:A:h}
A=$W/dist/Barrique.app/Contents/MacOS/barrique
secs=${1:-8}; n=${2:-4096}
out=${3:-$T/../../results/stress3d-$(date +%Y%m%d-%H%M).jsonl}
out=${out:A}
export BARRIQUE_DATA=${BARRIQUE_DATA:-$W/dist/test-move-2}
M=$W/llvm-mingw/bin
VK=$W/deps-src/moltenvk-1.4.2/External/Vulkan-Headers/include
mkdir -p $W/samples/stress3d ${out:h}
winpath() { print -r -- "Z:${1//\//\\}" }

for a in aarch64 x86_64 i686; do
    $M/$a-w64-mingw32-clang -O2 -I$VK -I$T -o $W/samples/stress3d/stress3d-$a.exe $T/stress3d.c \
        -ld3d9 -ld3d11 -ld3d12 -ldxgi -lopengl32 -lgdi32 -luser32 || exit 1
done
for a in aarch64 x86_64 i686; do
    exe=$W/samples/stress3d/stress3d-$a.exe
    print "== $a"
    timeout -k 5 600 $A --wine $exe all $secs $n --json $(winpath $out) 2>&1 | tr -d '\r' | grep -a -E '^(gl|d3d|vulkan|RESULT)'
    if [[ $a != i686 ]]; then
        BARRIQUE_D3D=dxmt timeout -k 5 300 $A --wine $exe d3d11 $secs $n --json $(winpath $out) 2>&1 | tr -d '\r' |
            grep -a -E '^d3d11' | sed 's/^/(dxmt) /'
    fi
done
print "\nmetrics: $out"
python3 -I $T/summary.py $out
