#!/bin/zsh
# Round 5 (phase 5 step 0): baseline of the graphics, audio, video and input modules
# before any phase 5 change.  run.sh av | gpu | all
export OUT=$HOME/Work/wine/tests/results/round5
R=$HOME/Work/wine/tests/run-conformance.sh
AV=(mmdevapi dsound winmm xaudio2_7 xaudio2_8 mf mfplat mfreadwrite mfmediaengine quartz wmvcore evr dxva2 windowscodecs dwrite dinput hid xinput1_3)
GPU=(d3d8 d3d9 d3d10core d3d11 d3d12 dxgi ddraw opengl32 d2d1 vulkan-1)
case ${1:-all} in
  av)  MODS=($AV) ;;
  gpu) MODS=($GPU) ;;
  *)   MODS=($AV $GPU) ;;
esac
for a in aarch64 x86_64 i386; do zsh $R $a $MODS; done
print ROUND5 ${1:-all} DONE
