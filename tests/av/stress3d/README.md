# stress3d

A 3D stress test of every graphics path in Barrique, with metrics.

One scene for all APIs: 4096 textured cubes in a grid (49 152 triangles), each with
its own rotation and color computed by the CPU every frame, and a 256×256 texture
rewritten every frame.

| API | Path | Draws |
|---|---|---|
| `gl` | Apple OpenGL 2.1 through winemac, fixed function | one per cube |
| `d3d9` | wined3d, fixed function | one per cube |
| `d3d11` | wined3d, or DXMT with `BARRIQUE_D3D=dxmt` | instanced, 16 cubes per draw |
| `d3d12` | vkd3d over MoltenVK | instanced |
| `vulkan` | MoltenVK | instanced |

Each frame uploads the instance data (80 bytes per cube, 320 KB) and the texture
(256 KB). A frame is read back before the timed run: the cubes must cover at least
2% of it.

## Running

    zsh run.sh [seconds] [instances] [output.jsonl]

`run.sh` builds `stress3d.c` for ARM64, x86-64 and x86 with llvm-mingw. It runs every
API from `Barrique.app` on each, and runs Direct3D 11 a second time with DXMT (not on
x86, where DXMT is not available). The metrics go to
`tests/results/stress3d-<date>.jsonl`, one JSON object per API, and `summary.py`
prints them as a table.

By hand:

    stress3d.exe [gl|d3d9|d3d11|d3d12|vulkan|all] [seconds] [instances]
                 [--batch n] [--size WxH] [--json file]

## Metrics (JSON fields)

| Field | Meaning |
|---|---|
| `fps`, `frames` | frames per second, vsync off |
| `frame_ms` | frame time: `avg`, `p50`, `p95`, `p99`, `max` |
| `first_frame_ms` | from the start of the API's setup to its first frame |
| `draws_per_s`, `mtris_per_s` | draw calls and millions of triangles per second |
| `upload_mb_s` | instance data and texture sent to the GPU per second |
| `cpu_user_s`, `cpu_kernel_s`, `cpu_use` | process CPU time during the timed run; `cpu_use` is in cores |
| `peak_ws_mb` | peak working set of the process |
| `coverage_pct` | how much of the frame that was read back the cubes cover |
| `ok`, `error` | check result |
| `adapter`, `backend` | the API's adapter string and the Barrique backend |

The Vulkan shaders are in `stress.vert` and `stress.frag`. `make-shaders.sh`
regenerates `spirv.h` from them with glslangValidator.
