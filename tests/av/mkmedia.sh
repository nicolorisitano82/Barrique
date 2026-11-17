#!/bin/zsh
# Generate the step 3 test media with the host ffmpeg (Homebrew, used only to
# make test files, never shipped) and their references (mkref.py).
#
#   mkmedia.sh <dir>
#
# Encoders change between ffmpeg versions: regenerate the .ref files together
# with the media, never one without the other.
set -e
d=${1:?usage: mkmedia.sh <dir>}; mkdir -p $d; cd $d
F=(ffmpeg -hide_banner -loglevel error -y)
V=(-f lavfi -i testsrc2=size=320x240:rate=25:duration=2)
A=(-f lavfi -i sine=frequency=440:sample_rate=44100:duration=2)
$F $V $A -c:v libx264 -pix_fmt yuv420p -c:a aac h264-aac.mp4
# H.264 High 4:4:4 (h264-444.mp4, kept in media-unsupported): the Microsoft H.264
# decoder only takes 4:2:0, and the source reader rejects it (0xc00d36bb) as on Windows
$F $V -c:v libx265 -pix_fmt yuv420p -tag:v hvc1 -x265-params log-level=error hevc.mp4
$F $V -c:v libvpx-vp9 -pix_fmt yuv420p vp9.webm
$F $A -c:a libmp3lame -b:a 128k mp3.mp3
$F $V $A -c:v wmv2 -b:v 800k -c:a wmav2 wmv2-wma2.wmv
$F $V $A -c:v mpeg2video -b:v 1M -c:a mp2 mpeg2.mpg
$F $A -c:a pcm_s16le pcm.wav
python3 ${0:A:h}/mkref.py *.mp4 *.webm *.mp3 *.wmv *.mpg *.wav
