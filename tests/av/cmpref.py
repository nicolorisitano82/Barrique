#!/usr/bin/env python3
"""Compare mfplay.exe output with the ffmpeg reference (mkref.py).

  cmpref.py <mfplay output> <file>.ref

Video: frame count within 1, every frame's mean Y within 2.0 (hardware
decoders and colour conversion are not bit exact). Audio: rate and channels
equal, sample frames within 3% (decoder priming and padding differ), RMS
within 5%. Prints one line per stream and RESULT ok/FAILED; exit code 1 on
failure.
"""
import sys


def parse(path):
    out = {}
    for line in open(path, errors="replace"):
        f = line.split()
        if f and f[0] in ("video", "audio", "error"):
            out.setdefault(f[0], []).append(f[1:])
    return out


got, ref = parse(sys.argv[1]), parse(sys.argv[2])
ok = True
for err in got.get("error", []):
    print("error", " ".join(err))
    ok = False
if "video" in ref:
    r = ref["video"][0]
    g = got.get("video", [["0"]])[0]
    rn, gn = int(r[0]), int(g[0])
    rm, gm = [float(x) for x in r[1:]], [float(x) for x in g[1:]]
    worst = max((abs(a - b) for a, b in zip(rm, gm)), default=0.0)
    good = abs(rn - gn) <= 1 and worst <= 2.0 and gn > 0
    print(f"video frames {gn}/{rn} max dY {worst:.2f} {'ok' if good else 'FAIL'}")
    ok &= good
if "audio" in ref:
    r = [float(x) for x in ref["audio"][0]]
    g = [float(x) for x in got.get("audio", [["0", "0", "0", "0"]])[0]]
    good = (g[1] == r[1] and g[2] == r[2] and abs(g[0] - r[0]) <= 0.03 * r[0]
            and abs(g[3] - r[3]) <= 0.05 * r[3])
    print(f"audio frames {int(g[0])}/{int(r[0])} rate {int(g[1])}/{int(r[1])} "
          f"ch {int(g[2])}/{int(r[2])} rms {g[3]:.0f}/{r[3]:.0f} {'ok' if good else 'FAIL'}")
    ok &= good
print("RESULT:", "ok" if ok else "FAILED")
sys.exit(0 if ok else 1)
