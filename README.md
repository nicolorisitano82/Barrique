# Barrique

Wine for Apple Silicon that runs Windows code natively, without Rosetta.

Windows code runs inside a Hypervisor.framework VM (4K pages, memory below 4GB,
x18 free, hardware TSO), with Wine's host side on macOS arm64. x86 code is
translated by [FEX](https://github.com/FEX-Emu/FEX): x86-64 through ARM64EC,
32-bit x86 through WoW64. ARM64 Windows programs run as they are.

**Status: development.** See [ROADMAP.md](ROADMAP.md) (in Italian) for what works,
what is being fixed and what comes next.

## Contents

| Path | What |
|---|---|
| `patches/wine/` | Changes to Wine 11.22 (tag `wine-11.22`): the Hypervisor backend (`dlls/ntdll/unix/hvf.c`), the 32-bit low memory window, wow64/win32u/winemac fixes |
| `fex/` | FEX source (upstream commit `ab135df` plus the submodules the Windows build needs) with Barrique's changes; see `fex/BARRIQUE.md` |
| `patches/fex/` | The same FEX changes as a diff against upstream |
| `entitlements.plist` | Entitlements the Wine loader needs (`com.apple.security.hypervisor`) |
| `tests/` | Conformance test runner, small PE test programs, latest i386 rerun summary |
| `research/hvproto/` | Hypervisor.framework prototypes used to design the backend |

## Building (outline)

1. Wine 11.22 source with `patches/wine/*.patch` applied; build an arm64 Wine
   with the ARM64EC and i386 PE targets, passing
   `aarch64_CFLAGS="-g -O2 -march=armv8.1-a" arm64ec_CFLAGS="-g -O2 -march=armv8.1-a"`
   to `configure` (LSE atomics, present on every Apple Silicon). The patch to
   `configure.ac` links the Hypervisor framework; run `autoreconf` if you
   don't use the patched `configure`.
2. FEX from `fex/`, cross-built with llvm-mingw for
   `aarch64-w64-mingw32` (`libwow64fex.dll`) and `arm64ec-w64-mingw32`
   (`libarm64ecfex.dll`), without LTO and with `-DTUNE_CPU=none`.
3. Sign the Wine loader with `entitlements.plist`, put the FEX DLLs where Wine
   finds builtin DLLs, run with `WINE_HVF=1`.

Build scripts and a single self-contained `.app` are planned (roadmap phase 6).

## Licenses

The Wine changes are under Wine's license, LGPL 2.1 or later. The FEX changes
are under FEX's license, MIT. Ideas taken from MetalSharp-Wine (AGPL-3.0) are
reimplemented, no code is copied.

The FEX project does not accept AI-generated code. The FEX changes here (in
`fex/` and `patches/fex/`) were written with an AI assistant: they are for
Barrique builds only and must not be submitted to FEX.
