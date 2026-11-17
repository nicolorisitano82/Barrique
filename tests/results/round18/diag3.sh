#!/bin/zsh
# Round 18 diagnosis 3, after: empty client area handled like a hidden window,
# vkDeviceWaitIdle before destroying a swapchain (MoltenVK crashed in its present
# handler on Metal's thread), faults on non-Wine threads crash instead of hanging,
# three swapchain images (CAMetalDrawable wait capped Vulkan at ~350 fps).
# (1) vulkan-1 aarch64 up to 4 runs, sample if one hangs; vulkan-1 x86_64, i386.
# (2) gears, all APIs, three architectures; d3d12 again with MoltenVK counters.
W=$HOME/Work/wine; S=$W/samples/phase5; R=$W/tests/results/round18/diag3
mkdir -p $R
export WINEPREFIX=$W/prefix-tests WINE_HVF=1 WINEDLLPATH=$W/fex-dlls WINEDLLOVERRIDES="mscoree=d;mshtml=d" WINEDEBUG=-all,err+hvf WINETEST_PLATFORM=wine
cd $W/build-arm64ec
for n in 1 2 3 4; do
    timeout -k 10 90 ./wine dlls/vulkan-1/tests/aarch64-windows/vulkan-1_test.exe vulkan > $R/vulkan-$n.log 2>&1 &
    runner=$!
    for s in {1..40}; do sleep 1; kill -0 $runner 2>/dev/null || break; done
    if kill -0 $runner 2>/dev/null; then
        print "run $n: hung, sampling"
        for p in $(pgrep -f "vulkan-1_test.exe"); do sample $p 3 -file $R/sample-$n-$p.txt >/dev/null 2>&1; done
        wait $runner
        continue
    fi
    wait $runner
    print "run $n: $(grep -a 'tests executed' $R/vulkan-$n.log | sed 's/.*vulkan: //') $(grep -ac 'not a Wine thread' $R/vulkan-$n.log) foreign-thread faults"
done
OUT=$R/conf zsh $W/tests/run-conformance.sh x86_64 vulkan-1
OUT=$R/conf zsh $W/tests/run-conformance.sh i386 vulkan-1
for a in aarch64 x86_64 i686; do
    timeout -k 10 150 ./wine $S/gears-$a.exe all 5 > $R/gears-$a.log 2>&1
    grep -a "^gl\|^d3d\|RESULT" $R/gears-$a.log | sed "s/^/$a /"
done
MVK_CONFIG_PERFORMANCE_TRACKING=1 MVK_CONFIG_PERFORMANCE_LOGGING_FRAME_COUNT=600 MVK_CONFIG_LOG_LEVEL=3 \
    timeout -k 10 60 ./wine $S/gears-aarch64.exe d3d12 4 > $R/gears-d3d12-mvkperf.log 2>&1
grep -a "Retrieve a CAMetalDrawable\|Frame interval" $R/gears-d3d12-mvkperf.log | tail -2
timeout 30 ./server/wineserver -k
print DIAG3 DONE
