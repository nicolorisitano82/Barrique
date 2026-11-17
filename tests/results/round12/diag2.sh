#!/bin/zsh
# Round 12, MPEG through winedmo + MF decoders: mfplay.exe on mpeg2.mpg on
# three architectures (aarch64 with MF traces) compared with its reference.
set -u
W=$HOME/Work/wine; R=$W/tests/results/round12; S=$W/samples/phase5; AV=$W/Barrique/tests/av
export WINEPREFIX=$W/prefix-tests WINE_HVF=1 WINEDLLPATH=$W/fex-dlls WINEDLLOVERRIDES="mscoree=d;mshtml=d" WINETEST_PLATFORM=wine
mkdir -p $R/after2
cd $W/build-arm64ec
for t in aarch64 x86_64 i686; do
    log=$R/after2/mfplay-$t-mpeg2.log
    if [[ $t == aarch64 ]]; then dbg=+mfplat,+dmo,+mfreadwrite; else dbg=-all; fi
    WINEDEBUG=$dbg timeout -k 10 120 ./wine $S/mfplay-$t.exe "Z:$S/media/mpeg2.mpg" > $log.full 2>&1
    rc=$?
    grep -a -v -e '^[0-9a-f]*:trace:' -e '^[0-9a-f]*:warn:' -e '^[0-9a-f]*:err:' -e '^[0-9a-f]*:fixme:' $log.full > $log
    python3 $AV/cmpref.py $log $S/media/mpeg2.mpg.ref > $log.cmp
    print "mfplay $t mpeg2.mpg rc=$rc $(tr '\n' ' ' < $log.cmp)"
    [[ $t == aarch64 ]] || rm -f $log.full
done
timeout -k 10 60 ./server/wineserver -k
print DIAG2 DONE
