#!/bin/zsh
# Round 16 diagnosis: vulkan-1 on three architectures with vulkan and seh
# traces, after the WoW64 command pool handle fix. Shows where the 64-bit
# runs die (vkGetPhysicalDeviceSurfaceFormats2KHR, no error printed).
export OUT=$HOME/Work/wine/tests/results/round16/diag
export WINEDEBUG=-all,err+hvf,+vulkan,warn+seh,err+vulkan
R=$HOME/Work/wine/tests/run-conformance.sh
for a in aarch64 x86_64 i386; do zsh $R $a vulkan-1; done
print DIAG DONE
