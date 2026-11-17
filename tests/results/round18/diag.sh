#!/bin/zsh
# Round 18 diagnosis (2nd: empty client area treated like a hidden window; MVK log level is numeric): (1) vulkan-1 aarch64 hangs now and then (created/hidden
# cases): up to 4 runs; when one is still going after 40 s, macOS `sample` takes
# the stacks of the test's wine processes before the timeout kills it.
# (2) MoltenVK's own performance tracking on gears d3d12 (CAMetalDrawable
# retrieval, submit, present) for the ~350 fps cap.
W=$HOME/Work/wine; S=$W/samples/phase5; R=$W/tests/results/round18/diag
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
        break
    fi
    wait $runner
    print "run $n: finished, $(grep -c 'Test failed' $R/vulkan-$n.log) failed checks"
done
timeout 30 ./server/wineserver -k
MVK_CONFIG_PERFORMANCE_TRACKING=1 MVK_CONFIG_PERFORMANCE_LOGGING_FRAME_COUNT=600 MVK_CONFIG_LOG_LEVEL=3 \
    timeout -k 10 60 ./wine $S/gears-aarch64.exe d3d12 4 > $R/gears-d3d12-mvkperf.log 2>&1
timeout 30 ./server/wineserver -k
print DIAG DONE
