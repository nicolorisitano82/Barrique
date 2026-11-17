#!/bin/zsh
# Round 16 diagnosis 11: i386 graphics after the WoW64/HVF fixes of the day
# (GL client objects, glShaderSource pointer array, GLintptr/GLsizeiptr no
# longer translated, vkMapMemory low window). Same units as diag7.
export OUT=$HOME/Work/wine/tests/results/round16/diag11
export WINEDEBUG=-all,err+hvf,warn+seh,err+opengl
R=$HOME/Work/wine/tests/run-conformance.sh
zsh $R i386 dxgi d3d11 d3d12 d3d10core d3d9 opengl32
print "host faults:"; grep -h "handle_syscall_fault" $OUT/logs/*.log 2>/dev/null | sed 's/.*host pc=[^ ]* //' | sort | uniq -c | head
print DIAG11 DONE
