#!/bin/zsh
# Copy the host libraries Wine dlopens into <wine root>/deps, with install
# names rewritten so nothing points at Homebrew: win32u.so finds them through
# its rpath @loader_path/../../deps (build tree: <build>/deps, install tree:
# <prefix>/lib/deps).
#
#   bundle-deps.sh <wine build or lib dir>
#
# Today: FreeType and its libpng (phase 5 step 1). Libraries are re-signed
# ad hoc after install_name_tool.
set -e
dest=${1:?usage: bundle-deps.sh <wine build or lib dir>}/deps
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

for lib in ${(k)libs}; do
    codesign -f -s - $dest/$lib 2>/dev/null
    if otool -L $dest/$lib | tail -n +2 | grep -v -e '^\s*/usr/lib/' -e '^\s*/System/' -e '@rpath/' -e '@loader_path/'; then
        print -u2 "$lib: external library path left"; exit 1
    fi
done
print "deps in $dest: ${(k)libs}"
