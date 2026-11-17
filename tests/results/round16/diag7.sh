#!/bin/zsh
# Round 16 diagnosis 7: i386 graphics after the glShaderSource fix
# (copy_wow64_ptr32s read the 32-bit pointer array at its Windows address).
export OUT=$HOME/Work/wine/tests/results/round16/diag7
export WINEDEBUG=-all,err+hvf,warn+seh
R=$HOME/Work/wine/tests/run-conformance.sh
zsh $R i386 dxgi d3d11 d3d12 d3d10core d3d9
print "host faults:"; grep -h "handle_syscall_fault" $OUT/logs/*.log 2>/dev/null | sed 's/.*host pc=[^ ]* //' | sort | uniq -c | head
print DIAG7 DONE
