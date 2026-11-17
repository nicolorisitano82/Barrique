#!/bin/zsh
# Round 16 diagnosis 3, the open points of phase 5 step 5:
#  1. vulkan-1 on three architectures after the surface_maintenance1 fix
#     (swapchain scaling, so MoltenVK stops returning VK_SUBOPTIMAL_KHR)
#  2. d3d12 on i386 with vkd3d warnings: why the device is not created
#  3. dxgi and d3d11 on aarch64 with wined3d's Vulkan renderer
#     (WINE_D3D_CONFIG=renderer=vulkan): does it reach feature level 10+?
R=$HOME/Work/wine/tests/run-conformance.sh
D=$HOME/Work/wine/tests/results/round16/diag3
export WINEDEBUG=-all,err+hvf,err+vulkan
for a in aarch64 x86_64 i386; do OUT=$D/vk zsh $R $a vulkan-1; done
OUT=$D/d3d12-i386 WINEDEBUG=-all,err+hvf,err+vulkan,warn+d3d12 VKD3D_DEBUG=warn zsh $R i386 d3d12
OUT=$D/wined3d-vk WINE_D3D_CONFIG=renderer=vulkan zsh $R aarch64 dxgi d3d11
print "refusals:"; grep -l "refusing to map host page" $D/*/logs/*.log 2>/dev/null
print DIAG3 DONE
