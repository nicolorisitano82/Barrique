#!/bin/zsh
# Copy the host libraries Wine dlopens or links into <wine root>/deps, with
# install names rewritten so nothing points at Homebrew: win32u.so,
# winegstreamer.so and winedmo.so find them through their rpath
# @loader_path/../../deps (build tree: <build>/deps, install tree:
# <prefix>/lib/deps).
#
#   bundle-deps.sh <wine build or lib dir> [media prefix]
#
# FreeType and its libpng come from Homebrew (phase 5 step 1). The media stack
# (FFmpeg, glib, GStreamer and its plugins, phase 5 step 3) comes from the
# prefix built by build-gst.sh (default ~/Work/wine/deps-gst), already with
# @rpath install names; plugins go to deps/gstreamer-1.0. Libraries are
# re-signed ad hoc after install_name_tool.
set -e
dest=${1:?usage: bundle-deps.sh <wine build or lib dir> [media prefix]}/deps
media=${2:-$HOME/Work/wine/deps-gst}
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

for lib in $copied; do
    codesign -f -s - $dest/$lib 2>/dev/null
    if otool -L $dest/$lib | tail -n +2 | grep -v -e '^\s*/usr/lib/' -e '^\s*/System/' -e '@rpath/' -e '@loader_path/'; then
        print -u2 "$lib: external library path left"; exit 1
    fi
done
print "deps in $dest: ${#copied} libraries"
