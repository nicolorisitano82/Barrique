#!/bin/zsh
# Round 16 diagnosis 4: why CreateDXGIFactory fails on i386 (wined3d's GL
# adapter is not created, DXGI_ERROR_UNSUPPORTED since rounds 5-6).
# dxgi on i386 with wined3d/wgl warnings and wgl traces; aarch64 dxgi as the
# working reference for the same channels.
export OUT=$HOME/Work/wine/tests/results/round16/diag4 UNIT_TIMEOUT=60
export WINEDEBUG=-all,err+all,warn+d3d,warn+wgl,trace+wgl,warn+opengl,warn+seh
R=$HOME/Work/wine/tests/run-conformance.sh
zsh $R i386 dxgi
zsh $R aarch64 dxgi
print DIAG4 DONE
