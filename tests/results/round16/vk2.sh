#!/bin/zsh
# Round 16 rerun after the HVF Vulkan fixes (handles, present rectangles, HWND): Vulkan run with MoltenVK 1.4.2 under HVF.
# vulkan-1 tests on three architectures; logs keep hvf/vulkan errors and the
# FIXME win32u prints when it takes the VK_EXT_external_memory_host path.
# Graphics modules after it (d3d11/d3d9 with wined3d's Vulkan off by default,
# so only to see nothing else changed) and the hvf refusals counted.
export OUT=$HOME/Work/wine/tests/results/round16/vk2
export WINEDEBUG=-all,err+hvf,err+vulkan,fixme+vulkan
R=$HOME/Work/wine/tests/run-conformance.sh
for a in aarch64 x86_64 i386; do zsh $R $a vulkan-1 d3d11 d3d12 dxgi; done
print "refusals:"; grep -l "refusing to map host page" $OUT/logs/*.log 2>/dev/null
print "external memory:"; grep -l "Using VK_EXT_external_memory_host" $OUT/logs/*.log 2>/dev/null
print VK DONE
