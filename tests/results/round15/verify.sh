#!/bin/zsh
# Round 15 (phase 5 step 4): hvf.c refuses GPU pages (IOAccelerator/IOSurface
# tags, shared + wired). Check nothing legitimate is refused: graphics modules
# as in round 6, VirtualLock (kernel32/ntdll virtual), and the hvf refusals
# (WINEDEBUG=-all,err+hvf) counted in every log. Compare with round6.
export OUT=$HOME/Work/wine/tests/results/round15/verify
export WINEDEBUG=-all,err+hvf
R=$HOME/Work/wine/tests/run-conformance.sh
GPU=(d3d8 d3d9 d3d10core d3d11 d3d12 dxgi ddraw opengl32 d2d1 vulkan-1)
for a in aarch64 x86_64 i386; do
    zsh $R $a $GPU
    UNITS=virtual zsh $R $a kernel32 ntdll
done
print "refusals:"
grep -l "refusing to map host page" $OUT/logs/*.log 2>/dev/null
grep -h "refusing to map host page" $OUT/logs/*.log 2>/dev/null | sed 's/.*VM: //' | sort | uniq -c | head -20
print VERIFY DONE
