#!/bin/zsh
# Round 16 diagnosis 8: the guest access violations left in i386 graphics
# (no host faults any more). Full seh traces (exception address, registers)
# and module loads, for the units that die fastest.
export OUT=$HOME/Work/wine/tests/results/round16/diag8 UNIT_TIMEOUT=90
export WINEDEBUG=-all,err+hvf,+seh,+loaddll
R=$HOME/Work/wine/tests/run-conformance.sh
UNITS=visual zsh $R i386 d3d9
zsh $R i386 d3d12
print DIAG8 DONE
