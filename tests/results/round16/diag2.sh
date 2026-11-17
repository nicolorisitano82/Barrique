#!/bin/zsh
# Round 16 diagnosis 2: after the WoW64 handle fixes (command pool, unwrap) and
# the vkGetPhysicalDevicePresentRectanglesKHR emulation (MoltenVK has none).
export OUT=$HOME/Work/wine/tests/results/round16/diag2
export WINEDEBUG=-all,err+hvf,+vulkan,warn+seh,err+vulkan
R=$HOME/Work/wine/tests/run-conformance.sh
for a in aarch64 x86_64 i386; do zsh $R $a vulkan-1; done
print DIAG DONE
