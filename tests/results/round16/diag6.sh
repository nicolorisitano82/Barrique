#!/bin/zsh
# Round 16 diagnosis 6: where d3d9 device/visual die on aarch64 (round 15:
# 19 and 18 failures; round16/diag5: crash and timeout) and where the i386
# units that now run (d3d9 device/d3d9ex/visual, d3d11, d3d12) crash.
# Exceptions and wined3d errors only.
export OUT=$HOME/Work/wine/tests/results/round16/diag6 UNIT_TIMEOUT=120
export WINEDEBUG=-all,err+all,warn+seh
R=$HOME/Work/wine/tests/run-conformance.sh
UNITS="device visual" zsh $R aarch64 d3d9
UNITS="device" zsh $R i386 d3d9
zsh $R i386 d3d11 d3d12
print DIAG6 DONE
