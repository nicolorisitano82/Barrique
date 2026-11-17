#!/bin/zsh
# Round 16 diagnosis 5: i386 graphics after the WoW64 fix for OpenGL client
# objects (contexts, pbuffers, syncs were dereferenced at their Windows
# address on the host, so every 32-bit GL context failed and wined3d had no
# adapter: CreateDXGIFactory returned DXGI_ERROR_UNSUPPORTED).
# Compare with round15/verify (i386) and round16/vk2.
export OUT=$HOME/Work/wine/tests/results/round16/diag5
export WINEDEBUG=-all,err+hvf
R=$HOME/Work/wine/tests/run-conformance.sh
zsh $R i386 dxgi d3d11 d3d12 d3d10core d3d9 opengl32
zsh $R aarch64 opengl32 d3d9
print "refusals:"; grep -l "refusing to map host page" $OUT/logs/*.log 2>/dev/null
print DIAG5 DONE
