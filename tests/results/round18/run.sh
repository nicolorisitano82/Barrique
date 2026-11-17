#!/bin/zsh
# Round 18: (1) vulkan-1 on three architectures after the hidden window fix
# (no host present when every window is hidden or minimized, images given back
# with vkReleaseSwapchainImagesEXT); (2) where a frame's time goes on the
# Vulkan path, ~350 fps cap: gears d3d12 and d3d9 on wined3d's Vulkan
# renderer with BARRIQUE_VK_TIMING=1 (temporary win32u counters), aarch64.
W=$HOME/Work/wine; S=$W/samples/phase5; R=$W/tests/results/round18
export OUT=$R/vk WINEDEBUG=-all,err+hvf,err+vulkan
for a in aarch64 x86_64 i386; do zsh $W/tests/run-conformance.sh $a vulkan-1; done
export WINEPREFIX=$W/prefix-tests WINE_HVF=1 WINEDLLPATH=$W/fex-dlls WINEDLLOVERRIDES="mscoree=d;mshtml=d" BARRIQUE_VK_TIMING=1
cd $W/build-arm64ec
timeout -k 10 60 ./wine $S/gears-aarch64.exe d3d12 5 > $R/gears-d3d12.log 2>&1
WINE_D3D_CONFIG=renderer=vulkan timeout -k 10 60 ./wine $S/gears-aarch64.exe d3d9 5 > $R/gears-d3d9vk.log 2>&1
timeout 30 ./server/wineserver -k
grep -h "barrique-vk-timing\|^d3d\|RESULT" $R/gears-*.log | sort | uniq -c | sort -rn | head -12
print ROUND18 DONE
