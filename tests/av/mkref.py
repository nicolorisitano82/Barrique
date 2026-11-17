#!/usr/bin/env python3
"""Reference values for mfplay.exe, decoded with the host ffmpeg.

  mkref.py <media file>...  -> <file>.ref next to each file

Per file: "video <frames> <mean Y of each frame...>" (Y plane, limited range,
frame size from the stream) and "audio <sample frames> <rate> <channels> <rms>" (16-bit
PCM at the stream's own rate and channels). mfplay.exe prints the same lines
and cmpref.py compares them with a tolerance.
"""
import json, subprocess, sys, array, math


def probe(path):
    out = subprocess.run(["ffprobe", "-v", "error", "-show_streams", "-of", "json", path],
                         check=True, capture_output=True).stdout
    return json.loads(out)["streams"]


def ffmpeg(args):
    return subprocess.run(["ffmpeg", "-v", "error", *args, "-"], check=True, capture_output=True).stdout


for path in sys.argv[1:]:
    lines = []
    streams = probe(path)
    v = next((s for s in streams if s["codec_type"] == "video"), None)
    a = next((s for s in streams if s["codec_type"] == "audio"), None)
    if v:
        w, h = v["width"], v["height"]
        # the Y plane as decoded (limited range, like mfplay.exe's NV12), not
        # "gray", which ffmpeg scales to full range (+2 on mean luma)
        raw = ffmpeg(["-i", path, "-map", "0:v:0", "-f", "rawvideo", "-pix_fmt", "yuv420p"])
        fs = w * h + 2 * ((w + 1) // 2) * ((h + 1) // 2)
        n = len(raw) // fs
        means = [sum(raw[i * fs:i * fs + w * h]) / (w * h) for i in range(n)]
        lines.append("video %d %s" % (n, " ".join("%.2f" % m for m in means)))
    if a:
        rate, ch = int(a["sample_rate"]), a["channels"]
        raw = array.array("h", ffmpeg(["-i", path, "-map", "0:a:0", "-f", "s16le", "-acodec", "pcm_s16le"]))
        rms = math.sqrt(sum(x * x for x in raw) / max(len(raw), 1))
        lines.append("audio %d %d %d %.1f" % (len(raw) // ch, rate, ch, rms))
    open(path + ".ref", "w").write("\n".join(lines) + "\n")
    print(path, "|", " | ".join(l[:60] for l in lines))
