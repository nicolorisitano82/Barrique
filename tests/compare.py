#!/usr/bin/env python3
"""Compare two conformance runs and list regressions.

  compare.py <baseline dir> <new dir> [arch-module ...]

Both directories hold run-conformance.sh output (<arch>-<module>.tsv and
logs/<arch>-<module>-<unit>.log). With no arch-module arguments, every tsv
present in both directories is compared.

A regression is:
  - a unit whose status got worse (ok -> fail/crash/timeout, fail -> crash/timeout),
  - a failing check (source file:line) that did not fail in the baseline.
Fixed checks and improved units are listed too. The exit code is 1 when
there is at least one regression, so the script can gate a step.
"""
import os, re, sys

RANK = {"ok": 0, "fail": 1, "timeout": 2, "crash": 3}
fail_re = re.compile(rb"^([\w.]+):(\d+):(?: Test failed:| Test succeeded inside todo block:)(.*)")


def units(path):
    out = {}
    with open(path) as f:
        next(f)
        for line in f:
            unit, status, executed, failures = line.rstrip("\n").split("\t")[:4]
            out[unit] = (status, executed, failures)
    return out


def checks(res, name, unit):
    path = os.path.join(res, "logs", f"{name}-{unit}.log")
    out = {}
    if os.path.exists(path):
        for line in open(path, "rb"):
            m = fail_re.match(line)
            if m:
                out.setdefault(f"{m[1].decode()}:{m[2].decode()}", m[3].decode(errors="replace").strip()[:90])
    return out


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    base, new = sys.argv[1], sys.argv[2]
    names = sys.argv[3:] or sorted(
        f[:-4] for f in os.listdir(new)
        if f.endswith(".tsv") and os.path.exists(os.path.join(base, f)))
    regressions = 0
    totals = {"worse": 0, "better": 0, "new": 0, "fixed": 0}
    for name in names:
        bu, nu = units(os.path.join(base, name + ".tsv")), units(os.path.join(new, name + ".tsv"))
        lines = []
        for unit in sorted(set(bu) | set(nu)):
            if unit not in nu:
                lines.append(f"  {unit}: missing in new run")
                regressions += 1
                continue
            if unit not in bu:
                lines.append(f"  {unit}: new unit, {nu[unit][0]}")
                continue
            (bs, _, bf), (ns, _, nf) = bu[unit], nu[unit]
            if RANK[ns] > RANK[bs]:
                lines.append(f"  {unit}: WORSE {bs}({bf}) -> {ns}({nf})")
                totals["worse"] += 1
                regressions += 1
            elif RANK[ns] < RANK[bs]:
                lines.append(f"  {unit}: better {bs}({bf}) -> {ns}({nf})")
                totals["better"] += 1
            bc, nc = checks(base, name, unit), checks(new, name, unit)
            added = sorted(set(nc) - set(bc))
            fixed = sorted(set(bc) - set(nc))
            # a unit that crashed in the baseline did not run all its checks:
            # failures after the crash point are not regressions
            if bs in ("crash", "timeout") and ns not in ("crash", "timeout"):
                if added:
                    lines.append(f"  {unit}: {len(added)} failing checks now reached (baseline {bs})")
                added = []
            for key in added:
                lines.append(f"  {unit}: NEW {key} {nc[key]}")
            if fixed:
                lines.append(f"  {unit}: {len(fixed)} checks fixed")
            totals["new"] += len(added)
            totals["fixed"] += len(fixed)
            regressions += len(added)
        if lines:
            print(f"== {name}")
            print("\n".join(lines))
    print(f"\nunits worse {totals['worse']}, better {totals['better']}; "
          f"failing checks new {totals['new']}, fixed {totals['fixed']}")
    print("REGRESSIONS" if regressions else "NO REGRESSIONS")
    return 1 if regressions else 0


if __name__ == "__main__":
    sys.exit(main())
