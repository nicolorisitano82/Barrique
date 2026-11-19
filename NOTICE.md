# Barrique: third-party software

Barrique.app contains the software listed here. Each is under its own license;
the license texts are in `Barrique.app/Contents/Resources/licenses/`, one folder
per component (made by `scripts/collect-licenses.sh`). Barrique's own files are
under the LGPL 2.1 or later (`LICENSE`, `COPYING.LIB`).

## Source code

Barrique's changes are in this repository: `patches/wine/` against Wine 11.22
(tag `wine-11.22`) and `fex/` for FEX. Every other component below is built
unmodified from the upstream source named in the table, by the scripts in
`scripts/`. To get the exact source of a release, check out the repository at
that release's tag: the scripts pin each version and checksum. For the LGPL
components, the build scripts are enough to rebuild them and replace the
libraries in the app (`Contents/Resources/wine/lib/deps`, plain dylibs with
`@rpath` names).

## Components

| Component | Version | License | Source | In the app |
|---|---|---|---|---|
| Wine | 11.22 with Barrique's patches | LGPL 2.1 or later | https://gitlab.winehq.org/wine/wine, `patches/wine/` | `Resources/wine` (most of it) |
| msync (part of the Wine patch) | from CrossOver 26.3 | LGPL 2.1 or later | https://www.codeweavers.com/crossover/source | `ntdll.so`, `wineserver` |
| FEX | upstream `ab135df` with Barrique's changes | MIT | https://github.com/FEX-Emu/FEX, `fex/` | `libarm64ecfex.dll`, `libwow64fex.dll` |
| FEX bundled code: cephes, fmt, range-v3, rpmalloc, tiny-json, unordered_dense, xxHash | as pinned by FEX | BSD / MIT / Boost / public domain, see each folder | FEX `External/` | same DLLs |
| DXMT | `0df8610` | LGPL 2.1 or later | https://github.com/3Shain/dxmt | `lib/wine/dxmt`, `winemetal.so` |
| LLVM (in DXMT's shader compiler) | 15.0.7 | Apache 2.0 with LLVM exceptions | https://github.com/llvm/llvm-project | `winemetal.so` |
| MoltenVK | 1.4.2 | Apache 2.0 | https://github.com/KhronosGroup/MoltenVK | `libMoltenVK.dylib` |
| MoltenVK bundled code: SPIRV-Cross, SPIRV-Tools, Volk, Vulkan-Headers, cereal | as pinned by MoltenVK | Apache 2.0 / MIT / BSD | MoltenVK `ExternalRevisions` | `libMoltenVK.dylib` |
| SDL2 | 2.32.10 | zlib | https://github.com/libsdl-org/SDL | `libSDL2-2.0.0.dylib` |
| FFmpeg (LGPL build, no GPL parts) | 9.0.2 | LGPL 2.1 or later | https://ffmpeg.org | `libav*`, `libsw*` |
| GStreamer, gst-plugins-base/good/bad/ugly, gst-libav | 1.28.9 | LGPL 2.1 or later | https://gstreamer.freedesktop.org | `libgst*`, `gstreamer-1.0/` |
| GLib | 2.90.1 | LGPL 2.1 or later | https://gitlab.gnome.org/GNOME/glib | `libglib`, `libgio`, `libgobject`, ... |
| PCRE2 (in GLib) | 10.49 | BSD | https://github.com/PCRE2Project/pcre2 | `libglib-2.0.0.dylib` |
| gettext libintl | 1.0 (Homebrew) | LGPL 2.1 or later | https://www.gnu.org/software/gettext | `libintl.8.dylib` |
| FreeType | 2.14.3 (Homebrew) | FreeType License (FTL) | https://freetype.org | `libfreetype.6.dylib` |
| libpng | 1.6.58 (Homebrew) | libpng license | http://www.libpng.org | `libpng16.16.dylib` |
| Wine Mono | 11.3.0 | MIT and others, see source | https://gitlab.winehq.org/mono/wine-mono | `share/wine/mono` |
| Wine Gecko | 2.47.4 | MPL 2.0 | https://gitlab.winehq.org/wine/wine-gecko | `share/wine/gecko` |
| Selawik font | 1.01 | SIL OFL 1.1 | https://github.com/microsoft/Selawik | `share/wine/fonts/selawk*.ttf` |
| Carlito, Caladea fonts | Google Fonts `7af0d90` | SIL OFL 1.1 | https://github.com/google/fonts | `share/wine/fonts` |
| Barrique Caption font | Barrique | SIL OFL 1.1 | `resources/fonts/make-caption-font.py` | `share/wine/fonts/barrique-caption.ttf` |

Portions of this software are copyright © 2026 The FreeType Project
(https://freetype.org). All rights reserved.

## Notes

- Wine Mono and Wine Gecko are shipped as the binary packages WineHQ publishes
  for Wine; their sources are at the links above.
- The FEX project does not accept AI-generated code. Barrique's FEX changes
  were written with an AI assistant: they are for Barrique builds only and must
  not be submitted to FEX.
- Barrique is not affiliated with Microsoft, Apple, WineHQ, CodeWeavers, the
  FEX project or the authors of the components above.
