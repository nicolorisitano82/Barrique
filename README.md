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
| `tests/` | Conformance test runner (aarch64, x86_64, i386) and failure classifier, small PE test programs, real-program probes (`apps/`), results |
| `research/hvproto/` | Hypervisor.framework prototypes used to design the backend |

## Building (outline)

macOS 26 or later on Apple Silicon, Xcode 26+, llvm-mingw, Homebrew for build
tools only (bison, meson, ninja, cmake, pkg-config; FreeType/libpng are copied in).

1. Dependencies, each built by a script in `scripts/` from checksummed source
   tarballs: `build-gst.sh` (FFmpeg LGPL, GStreamer, gst-libav, applemedia),
   `build-moltenvk.sh` (MoltenVK), `build-llvm15.sh` and `build-dxmt.sh` (DXMT,
   built unmodified), `fetch-fonts.sh` (Selawik, Carlito, Caladea).
2. Wine 11.22 source with `patches/wine/*.patch` applied, configured as:

       PKG_CONFIG_PATH=<deps-gst>/lib/pkgconfig \
       CFLAGS="-g -O2 -mmacosx-version-min=26.0" \
       LDFLAGS="-L<deps-vk>/lib -mmacosx-version-min=26.0" \
       configure --enable-win64 --enable-archs=arm64ec,aarch64,i386 \
         aarch64_CFLAGS="-g -O2 -march=armv8.1-a" arm64ec_CFLAGS="-g -O2 -march=armv8.1-a" \
         --without-x --without-usb (plus the other --without-* of a macOS build)

   (LSE atomics are present on every Apple Silicon.) The patch to `configure.ac`
   links the Hypervisor framework; run `autoreconf` if you don't use the patched
   `configure`.
3. FEX from `fex/`, cross-built with llvm-mingw for `aarch64-w64-mingw32`
   (`libwow64fex.dll`) and `arm64ec-w64-mingw32` (`libarm64ecfex.dll`), without
   LTO and with `-DTUNE_CPU=none`.
4. `scripts/make-dist.sh` builds `dist/Barrique.app`: Wine installed inside the
   bundle, the libraries in `lib/deps` with `@rpath` names, FEX, DXMT, fonts,
   debug information stripped, every Mach-O signed ad hoc and the loader
   (`lib/wine/aarch64-unix/wine`) with `entitlements.plist`; then it checks that
   every Mach-O is arm64-only, for macOS 26 or older, with no library path
   outside the bundle.

Running: `Barrique.app/Contents/MacOS/barrique program.exe` (or `--wine <command>`).
Data lives in `~/Library/Application Support/Barrique` (the prefix is created on
first run); `BARRIQUE_D3D=dxmt` selects DXMT for Direct3D 10/11.

## Licenses

The Wine changes are under Wine's license, LGPL 2.1 or later. The FEX changes
are under FEX's license, MIT. Ideas taken from MetalSharp-Wine (AGPL-3.0) are
reimplemented, no code is copied.

The FEX project does not accept AI-generated code. The FEX changes here (in
`fex/` and `patches/fex/`) were written with an AI assistant: they are for
Barrique builds only and must not be submitted to FEX.
