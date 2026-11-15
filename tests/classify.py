#!/usr/bin/env python3
"""Group conformance failures by which architectures show them.

  classify.py <results dir> [module...]

For every unit, each failing check (source file:line) is put in a bucket:
  all      fails on aarch64, x86_64 and i386  -> Wine / macOS / VM, not the translator
  x86      x86_64 and i386 only               -> FEX
  x86_64   x86_64 only                        -> ARM64EC side (FEX or Wine)
  i386     i386 only                          -> WoW64 / 32-bit window
  aarch64  aarch64 only                       -> native ARM64 path
  other    aarch64 plus one x86 arch
Units that crashed or timed out are listed separately: their checks are incomplete.
"""
import collections, csv, os, re, sys

ARCHS = ("aarch64", "x86_64", "i386")
res = sys.argv[1]
mods = sys.argv[2:] or sorted({f.split("-", 1)[1][:-4] for f in os.listdir(res) if f.endswith(".tsv")})
fail_re = re.compile(rb"^([\w.]+):(\d+):(?: Test failed:| Test succeeded inside todo block:)(.*)")

def failures(arch, mod, unit):
    path = os.path.join(res, "logs", f"{arch}-{mod}-{unit}.log")
    out = {}
    if os.path.exists(path):
        for line in open(path, "rb"):
            m = fail_re.match(line)
            if m:
                key = f"{m[1].decode()}:{m[2].decode()}"
                out.setdefault(key, m[3].decode(errors="replace").strip()[:90])
    return out

totals = collections.Counter()
for mod in mods:
    status = {}
    for arch in ARCHS:
        tsv = os.path.join(res, f"{arch}-{mod}.tsv")
        if os.path.exists(tsv):
            for row in csv.DictReader(open(tsv), delimiter="\t"):
                status[arch, row["unit"]] = row
    units = sorted({u for _, u in status})
    for unit in units:
        sts = {a: status.get((a, unit), {}).get("status", "-") for a in ARCHS}
        if all(s in ("ok", "-") for s in sts.values()):
            continue
        fails = {a: failures(a, mod, unit) for a in ARCHS}
        buckets = collections.defaultdict(list)
        for key in sorted(set().union(*fails.values())):
            where = tuple(a for a in ARCHS if key in fails[a])
            name = {ARCHS: "all", ("x86_64", "i386"): "x86", ("x86_64",): "x86_64",
                    ("i386",): "i386", ("aarch64",): "aarch64"}.get(where, "other:" + "+".join(where))
            msg = next(fails[a][key] for a in where)
            buckets[name].append(f"{key} {msg}")
        print(f"\n== {mod} {unit}  " + "  ".join(f"{a}={sts[a]}({len(fails[a])})" for a in ARCHS))
        for name in ("all", "x86", "x86_64", "i386", "aarch64") + tuple(k for k in buckets if k.startswith("other")):
            if buckets.get(name):
                totals[name] += len(buckets[name])
                print(f"  [{name}] {len(buckets[name])}")
                for l in buckets[name][:6]:
                    print(f"     {l}")
                if len(buckets[name]) > 6:
                    print(f"     ... {len(buckets[name]) - 6} more")
        for a in ARCHS:
            if sts[a] in ("crash", "timeout"):
                totals[f"{sts[a]}:{a}"] += 1
print("\nTotals (distinct failing checks per bucket; crashes/timeouts per arch):")
for k, v in sorted(totals.items()):
    print(f"  {k:16} {v}")
