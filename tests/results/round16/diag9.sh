#!/bin/zsh
# Round 16 diagnosis 9: why buffer mappings return NULL on i386
# (d3d9 visual: wined3d memmove into a NULL GL mapping; d3d12: readback
# through a NULL vkMapMemory pointer). opengl / vulkan traces, short runs.
export OUT=$HOME/Work/wine/tests/results/round16/diag9 UNIT_TIMEOUT=60
R=$HOME/Work/wine/tests/run-conformance.sh
UNITS=visual WINEDEBUG=-all,err+hvf,+opengl,warn+seh zsh $R i386 d3d9
WINEDEBUG=-all,err+hvf,+vulkan,warn+seh zsh $R i386 d3d12
print DIAG9 DONE
