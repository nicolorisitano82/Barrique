# Barrique stress3d: table from a JSON lines file written by stress3d.exe --json
import json, sys

rows = [json.loads(l) for l in open(sys.argv[1]) if l.strip()]
cols = ("arch", "api", "backend", "ok", "fps", "p99", "draws/s", "Mtri/s", "MB/s", "cpu", "ws MB", "first ms")
print("%-7s %-6s %-9s %-4s %9s %8s %10s %8s %8s %5s %6s %8s" % cols)
for r in rows:
    ok = "ok" if r["ok"] else "FAIL"
    if not r["frames"]:
        print("%-7s %-6s %-9s %-4s  %s" % (r["arch"], r["api"], r["backend"], ok, r["error"]))
        continue
    print("%-7s %-6s %-9s %-4s %9.1f %8.2f %10.0f %8.1f %8.1f %5.2f %6.0f %8.0f" % (
        r["arch"], r["api"], r["backend"], ok, r["fps"], r["frame_ms"]["p99"], r["draws_per_s"],
        r["mtris_per_s"], r["upload_mb_s"], r["cpu_use"], r["peak_ws_mb"], r["first_frame_ms"]))
