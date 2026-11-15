#!/bin/zsh
# Round 4 (phase 4 close-out): ntdll+kernel32 on all three architectures with the phase 4 fixes.
export OUT=$HOME/Work/wine/tests/results/round4
R=$HOME/Work/wine/tests/run-conformance.sh
for a in aarch64 x86_64 i386; do zsh $R $a ntdll kernel32; done
print ROUND4 DONE
