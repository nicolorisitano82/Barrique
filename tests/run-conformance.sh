#!/bin/zsh
# Run Wine conformance test units one by one under the Hypervisor.framework backend.
#
#   run-conformance.sh <arch> <module>...     arch: aarch64 | i386
#
# Each unit runs as its own process with a timeout. Results go to
# tests/results/<arch>-<module>.tsv (one line per unit) and the full output
# of each unit to tests/results/logs/<arch>-<module>-<unit>.log.

set -u
zmodload zsh/datetime   # EPOCHSECONDS
arch=$1; shift
WINE_ROOT=${WINE_ROOT:-$HOME/Work/wine}
BUILD=$WINE_ROOT/build-arm64ec
OUT=$WINE_ROOT/tests/results
UNIT_TIMEOUT=${UNIT_TIMEOUT:-180}

export WINEPREFIX=${WINEPREFIX:-$WINE_ROOT/prefix-tests}
export WINE_HVF=1
export WINEDLLPATH=$WINE_ROOT/fex-dlls
export WINEDLLOVERRIDES="mscoree=d;mshtml=d"
export WINEDEBUG=${WINEDEBUG:--all}
export WINETEST_PLATFORM=wine

mkdir -p $OUT/logs
cd $BUILD

for module in "$@"; do
    exe=dlls/$module/tests/$arch-windows/${module}_test.exe
    tsv=$OUT/$arch-$module.tsv
    print "unit\tstatus\texecuted\tfailures\ttodo\tskipped\tseconds" > $tsv
    units=(${(f)"$(grep -o 'func_[a-z0-9_]*' dlls/$module/tests/testlist.c | sort -u | sed 's/func_//')"})
    for unit in $units; do
        log=$OUT/logs/$arch-$module-$unit.log
        start=$EPOCHSECONDS
        timeout -k 10 $UNIT_TIMEOUT ./wine $exe $unit > $log 2>&1
        rc=$?
        secs=$(( EPOCHSECONDS - start ))
        # main summary line of the unit: "xxxx:unit: N tests executed (T marked as todo, F as flaky, E failures), S skipped."
        summary=$(grep -a ":$unit: [0-9]* tests executed" $log | tail -1)
        if [[ $rc -eq 124 ]]; then st=timeout
        elif [[ -z $summary ]]; then st=crash
        elif grep -aq "Unhandled exception\|Unhandled page fault" $log; then st=crash
        else st=done
        fi
        executed=$(print -r -- $summary | sed -n 's/.*: \([0-9]*\) tests executed.*/\1/p')
        todo=$(print -r -- $summary | sed -n 's/.*(\([0-9]*\) marked as todo.*/\1/p')
        failures=$(print -r -- $summary | sed -n 's/.* \([0-9]*\) failures).*/\1/p')
        skipped=$(print -r -- $summary | sed -n 's/.*), \([0-9]*\) skipped.*/\1/p')
        [[ $st == done && ${failures:-0} -gt 0 ]] && st=fail
        [[ $st == done ]] && st=ok
        print "$unit\t$st\t${executed:--}\t${failures:--}\t${todo:--}\t${skipped:--}\t$secs" >> $tsv
        print "$arch $module $unit: $st (${executed:--} tests, ${failures:--} failures, ${secs}s)"
        # a timed-out or crashed unit may leave processes behind: start the next one clean
        if [[ $st == timeout || $st == crash ]]; then
            ./server/wineserver -k 2>/dev/null
            pkill -9 -f "${module}_test.exe" 2>/dev/null
        fi
    done
done
./server/wineserver -w
