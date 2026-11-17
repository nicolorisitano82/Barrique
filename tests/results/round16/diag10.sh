#!/bin/zsh
# Round 16 diagnosis 10: after the vkMapMemory low-window fix; GL allocation status.
export OUT=$HOME/Work/wine/tests/results/round16/diag10 UNIT_TIMEOUT=60
R=$HOME/Work/wine/tests/run-conformance.sh
UNITS=visual WINEDEBUG=-all,err+hvf,warn+opengl,err+opengl,warn+seh zsh $R i386 d3d9
WINEDEBUG=-all,err+hvf,err+vulkan,fixme+vulkan,warn+seh zsh $R i386 d3d12
print DIAG10 DONE
