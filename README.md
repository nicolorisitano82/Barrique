# Barrique

Wine for Apple Silicon that runs Windows code natively, without Rosetta.

Windows code runs inside a Hypervisor.framework VM (4K pages, memory below 4GB,
x18 free, hardware TSO), with Wine's host side on macOS arm64. x86 code is
translated by [FEX](https://github.com/FEX-Emu/FEX): x86-64 through ARM64EC,
32-bit x86 through WoW64. ARM64 Windows programs run as they are.

**Status: alpha.** Releases: [GitHub releases](https://github.com/nicolorisitano82/Barrique/releases).
See [ROADMAP.md](ROADMAP.md) (in Italian) for what works, what is being fixed
and what comes next.

## Why Barrique

- **No Rosetta.** Wine's host side is a native arm64 macOS program, and Windows
  code runs in a hardware VM, not through Rosetta. x86 Windows programs are
  translated by FEX, an open-source translator Barrique ships and maintains.
- **ARM64 Windows programs need no translation at all:** Firefox and other
  programs with ARM64 builds run their own code directly, as on Windows on Arm.
- **Windows' own memory model, done in hardware.** Inside the VM, Windows
  programs get 4K pages, a 32-bit address space below 4 GB for 32-bit
  programs, and the x86 memory ordering (TSO) of Apple Silicon. This is what
  Windows code expects, so there is no emulation of these details in software.
- **Modern graphics on Metal:**
  - Direct3D 10/11 on Metal through DXMT;
  - Vulkan through MoltenVK, Direct3D 12 through vkd3d;
  - OpenGL and Direct3D 9 through Wine's wined3d.
- **Real programs, not only demos:**
  - Steam (login, library, downloads) and games: Muck (Direct3D 11), Portal and
    Tomb Raider (32-bit, Direct3D 9);
  - Firefox and Google Chrome with their sandboxes on;
  - .NET programs, installers, USB disks, USB serial devices.
- **One self-contained app.** `Barrique.app` holds Wine, FEX, DXMT, MoltenVK,
  GStreamer/FFmpeg and fonts. It needs no Homebrew and no Xcode, and keeps all
  its data in one folder.
- **Open source**, LGPL 2.1 or later, built from checksummed upstream sources.

## Performance

Measured on an Apple M3 Max with macOS 27, with Barrique 0.1.0-alpha.3. These
are alpha numbers: expect them to move.

| What | Result |
|---|---|
| Firefox ARM64, WebGL (hardware, DXMT) | 120 fps, Chromium sandbox on |
| Firefox, start and a JS/canvas benchmark page | ARM64 ~21 s; x86-64 (FEX) 25–27 s, from ~36 s before the FEX disk cache |
| Tomb Raider (2013, 32-bit x86, D3D9), built-in benchmark, medium quality | 35 fps average (14 min, 47 max) at 3024×1964; the same at a quarter of the pixels, so the limit is the CPU, not the GPU |
| Direct3D 11 (DXMT) stress test, `tests/av/stress3d` | ~600 fps ARM64, ~596 fps x86-64 |
| OpenTTD, 50,000 ticks without a window (CPU only) | 4.4 s ARM64, 5.3 s x86-64, 5.5–5.9 s 32-bit x86 |
| Program start (`hello`), warm / cold | 0.04–0.08 s / ~0.5 s |
| A trivial Windows system call | 1.18 µs ARM64, 1.40 µs x86, 1.68 µs x86-64 (native macOS: ~0.1 µs) |
| Mutex, semaphore, event wait (`WINEMSYNC=1`) | 3.6–4 µs, instead of 26–32 µs through the wineserver |
| First touch of new memory | 2.45 µs per 4K page (512 MB in ~320 ms) |
| Wine conformance tests (`ntdll`, `kernel32`, ARM64 and x86) | 99.994% of ~17.7 million checks pass |

Where the time goes, and what is still slow:
- **Leaving the VM** costs about 1 µs on every system call, and the hardware
  itself takes about two thirds of that. Programs that make very many system
  calls pay for it: Tomb Raider's main thread spends about a third of its time
  in system calls and in entering and leaving the VM.
- **x86 translation** (FEX) is the main cost of x86-64 and 32-bit programs.
  Code that is new to FEX causes stutter the first time it runs; translated
  code is kept on disk and reused later. JIT-heavy x86 code is the worst case:
  a Java benchmark runs 3.6× slower than native.
- **Direct3D 9 and OpenGL** go through Apple's OpenGL, which is limited.
  Direct3D 10/11 and Vulkan programs are better served by DXMT and MoltenVK.

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
         --without-x (plus the other --without-* of a macOS build), with
         USB_CFLAGS/USB_LIBS pointing at libusb from scripts/build-libusb.sh

   (LSE atomics are present on every Apple Silicon.) The patch to `configure.ac`
   links the Hypervisor framework; run `autoreconf` if you don't use the patched
   `configure`.
3. FEX from `fex/`, cross-built with llvm-mingw for `aarch64-w64-mingw32`
   (`libwow64fex.dll`) and `arm64ec-w64-mingw32` (`libarm64ecfex.dll`) by
   `scripts/build-fex.sh`: no LTO, `-DTUNE_CPU=none`, and `-DHAS_CLANG_PRESERVE_ALL=0`
   (clang's `preserve_all` does not hold for ARM64EC: x87 instructions such as
   `fyl2x` lost x86-64 registers, `tests/pe/x87regs.c`).
4. `scripts/make-dist.sh` builds `dist/Barrique.app`: Wine installed inside the
   bundle, the libraries in `lib/deps` with `@rpath` names, FEX, DXMT, fonts,
   debug information stripped, every Mach-O signed ad hoc and the loader
   (`lib/wine/aarch64-unix/wine`) with `entitlements.plist`; then it checks that
   every Mach-O is arm64-only, for macOS 26 or older, with no library path
   outside the bundle.

Running: `Barrique.app/Contents/MacOS/barrique program.exe` (or `--wine <command>`).
Data lives in `~/Library/Application Support/Barrique` (the prefix is created on
first run). Direct3D 10/11 uses DXMT by default (64-bit programs; 32-bit ones fall
back to wined3d); `BARRIQUE_D3D=wined3d` selects wined3d.

## Installing a release

See [docs/README-alpha.txt](docs/README-alpha.txt) (also in the release zip):
requirements, the Gatekeeper step for an app that is not notarized, known problems.
Releases are packed by `scripts/make-release.sh <version>` after `make-dist.sh`.

## Licenses

Barrique is under the LGPL 2.1 or later ([LICENSE](LICENSE), [COPYING.LIB](COPYING.LIB)).
The Wine changes are under Wine's license, LGPL 2.1 or later. The FEX changes
are under FEX's license, MIT. The Barrique Caption font is under the SIL OFL 1.1.
Third-party software in the app and its licenses: [NOTICE.md](NOTICE.md).
Ideas taken from MetalSharp-Wine (AGPL-3.0) are reimplemented, no code is copied.

The FEX project does not accept AI-generated code. The FEX changes here (in
`fex/` and `patches/fex/`) were written with an AI assistant: they are for
Barrique builds only and must not be submitted to FEX.
