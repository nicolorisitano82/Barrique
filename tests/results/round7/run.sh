#!/bin/zsh
# Round 7 (phase 5 step 1, FreeType and fonts): run.sh base | after
# base = before the change (no deps/), after = FreeType loaded from build-arm64ec/deps.
export OUT=$HOME/Work/wine/tests/results/round7/${1:-after}
R=$HOME/Work/wine/tests/run-conformance.sh
MODS=(gdi32 user32 dwrite d2d1 usp10)
mkdir -p $OUT
for a in aarch64 x86_64 i386; do zsh $R $a $MODS; done
print ROUND7 ${1:-after} DONE
