#!/bin/zsh
# Copy the host libraries Wine dlopens or links into <wine root>/deps, with
# install names rewritten so nothing points at Homebrew: win32u.so,
# winegstreamer.so and winedmo.so find them through their rpath
# @loader_path/../../deps (build tree: <build>/deps, install tree:
# <prefix>/lib/deps).
#
#   bundle-deps.sh <wine build or lib dir> [media prefix] [fonts dir] [vulkan prefix]
#
# FreeType and its libpng come from Homebrew (phase 5 step 1). The media stack
# (FFmpeg, glib, GStreamer and its plugins, phase 5 step 3) comes from the
# prefix built by build-gst.sh (default ~/Work/Personale/wine/deps-gst), already with
# @rpath install names; plugins go to deps/gstreamer-1.0. Libraries are
# re-signed ad hoc after install_name_tool.
#
# The fonts with Windows metrics (fetch-fonts.sh, default ~/Work/Personale/wine/deps-fonts,
# phase 5 step 1b) go to <wine build>/fonts, the directory Wine loads its own
# fonts from in a build tree (share/wine/fonts in an install tree).
set -e
dest=${1:?usage: bundle-deps.sh <wine build or lib dir> [media prefix]}/deps
media=${2:-$HOME/Work/Personale/wine/deps-gst}
fonts=${3:-$HOME/Work/Personale/wine/deps-fonts}
vk=${4:-$HOME/Work/Personale/wine/deps-vk}   # MoltenVK from build-moltenvk.sh (phase 5 step 5)
src=${HOMEBREW_PREFIX:-/opt/homebrew}/opt
mkdir -p $dest

typeset -A libs
libs=(libfreetype.6.dylib freetype libpng16.16.dylib libpng)

for lib formula in ${(kv)libs}; do
    install -m 644 $src/$formula/lib/$lib $dest/$lib
    install_name_tool -id @rpath/$lib $dest/$lib 2>/dev/null
done

# FreeType -> libpng next to it
install_name_tool -change $src/libpng/lib/libpng16.16.dylib @loader_path/libpng16.16.dylib \
    $dest/libfreetype.6.dylib 2>/dev/null

copied=(${(k)libs})
if [[ -d $media/lib ]]; then
    # each library under the name it is referred to by, its install id
    # (libavcodec.63.dylib, libglib-2.0.0.dylib), not the dev symlinks
    for f in $media/lib/lib*.dylib(.N); do
        id=$(otool -D $f | tail -1)
        install -m 644 $f $dest/${id:t}
        copied+=(${id:t})
    done
    mkdir -p $dest/gstreamer-1.0
    for f in $media/lib/gstreamer-1.0/*.dylib(N); do
        install -m 644 $f $dest/gstreamer-1.0/${f:t}
        copied+=(gstreamer-1.0/${f:t})
    done
fi

# GnuTLS from Homebrew, with every Homebrew library it needs (nettle, gmp, libtasn1,
# libidn2, libunistring, p11-kit, libintl): secur32.so (TLS for WinINet, WinHTTP,
# Steam...) and crypt32.so dlopen it. Homebrew's libintl replaces the media one: it is
# newer (gnutls needs compatibility version 13, glib 10).
typeset -A seen
todo=($src/gnutls/lib/libgnutls.30.dylib)
while (( ${#todo} )); do
    f=$todo[1]; shift todo
    leaf=${f:t}
    [[ -n ${seen[$leaf]} ]] && continue
    seen[$leaf]=1
    install -m 644 ${f:A} $dest/$leaf
    install_name_tool -id @rpath/$leaf $dest/$leaf 2>/dev/null
    for dep in ${(f)"$(otool -L $f | tail -n +2 | awk '{print $1}' | grep '^/opt/homebrew/')"}; do
        install_name_tool -change $dep @loader_path/${dep:t} $dest/$leaf 2>/dev/null
        todo+=($dep)
    done
    (( ${copied[(Ie)$leaf]} )) || copied+=($leaf)
done

# SDL2 from build-sdl2.sh: winebus.sys's SDL backend for game controllers
sdl=${SDL2_PREFIX:-$HOME/Work/Personale/wine/deps-sdl2}
if [[ -f $sdl/lib/libSDL2-2.0.0.dylib ]]; then
    install -m 644 $sdl/lib/libSDL2-2.0.0.dylib $dest/libSDL2-2.0.0.dylib
    copied+=(libSDL2-2.0.0.dylib)
fi

if [[ -f $vk/lib/libMoltenVK.dylib ]]; then
    install -m 644 $vk/lib/libMoltenVK.dylib $dest/libMoltenVK.dylib
    copied+=(libMoltenVK.dylib)
fi

for lib in $copied; do
    codesign -f -s - $dest/$lib 2>/dev/null
    if otool -L $dest/$lib | tail -n +2 | grep -v -e '^\s*/usr/lib/' -e '^\s*/System/' -e '@rpath/' -e '@loader_path/'; then
        print -u2 "$lib: external library path left"; exit 1
    fi
done
nfonts=0
if [[ -d $fonts ]]; then
    fdest=${dest:h}/fonts
    [[ -d ${dest:h:h}/share/wine ]] && fdest=${dest:h:h}/share/wine/fonts   # install tree: <prefix>/lib/deps
    mkdir -p $fdest
    for f in $fonts/*.ttf(N); do install -m 644 $f $fdest/${f:t}; (( ++nfonts )); done
fi
# Barrique's own caption glyphs (Segoe Fluent Icons / MDL2 Assets replacement, prefix-defaults.reg)
fdest=${dest:h}/fonts
[[ -d ${dest:h:h}/share/wine ]] && fdest=${dest:h:h}/share/wine/fonts
mkdir -p $fdest
python3 -I ${0:A:h}/../resources/fonts/make-caption-font.py $fdest/barrique-caption.ttf && (( ++nfonts ))
print "deps in $dest: ${#copied} libraries; $nfonts fonts"
