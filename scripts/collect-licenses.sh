#!/bin/zsh
# Copies the license texts of everything shipped in Barrique.app into
# <dest> (Barrique.app/Contents/Resources/licenses), one folder per component.
# NOTICE.md (repository root) says what each component is, its version, license
# and where its source is.
#
#   collect-licenses.sh <dest>
#
# Fails if a license file is missing, so a new dependency or version cannot ship
# without its license.
set -e
dest=${1:?destination folder}
W=$HOME/Work/Personale/wine
D=$W/deps-src
S=${0:A:h}
missing=0

lic() {   # lic <component> <file>...
    local c=$1; shift
    mkdir -p $dest/$c
    for f in "$@"; do
        if [[ -f $f ]]; then cp $f $dest/$c/
        else print "missing license: $c: $f"; missing=1; fi
    done
}

rm -rf $dest; mkdir -p $dest
lic wine           $W/src/LICENSE $W/src/COPYING.LIB $W/src/AUTHORS
lic fex            $W/fex/LICENSE
for x in cephes fmt range-v3 rpmalloc tiny-json unordered_dense xxhash; do
    lic fex/$x     $W/fex/External/$x/LICENSE*(N[1])
done
lic dxmt           $D/dxmt-0df8610/LICENSE
lic llvm           $D/llvm-15.0.7/llvm/LICENSE.TXT
lic moltenvk       $D/moltenvk-1.4.2/LICENSE
for x in SPIRV-Cross SPIRV-Tools Volk Vulkan-Headers cereal; do
    lic moltenvk/$x $D/moltenvk-1.4.2/External/$x/LICENSE*(N[1])
done
lic sdl2           $D/SDL2-2.32.10/LICENSE.txt
lic libusb         $D/libusb-1.0.30/COPYING
lic ffmpeg         $D/ffmpeg-9.0.2/LICENSE.md $D/ffmpeg-9.0.2/COPYING.LGPLv2.1
lic glib           $D/glib-2.90.1/COPYING
lic pcre2          $D/pcre2-10.49/LICENCE.md
for x in gstreamer gst-plugins-base gst-plugins-good gst-plugins-bad gst-plugins-ugly gst-libav; do
    lic gstreamer/$x $D/$x-1.28.9/COPYING
done
lic freetype       /opt/homebrew/Cellar/freetype/2.14.3/LICENSE.TXT
# FTL.TXT is not in the Homebrew bottle: from the FreeType source tarball
lic freetype       $D/freetype-2.14.3/docs/FTL.TXT
lic libpng         /opt/homebrew/Cellar/libpng/1.6.58/LICENSE
lic gettext-libintl $W/src/COPYING.LIB   # libintl is LGPL 2.1 or later, same text as Wine's
# GnuTLS and the Homebrew libraries it needs (bundle-deps.sh)
B=/opt/homebrew/Cellar
lic gnutls         $B/gnutls/3.8.13_2/COPYING.LESSERv2
lic nettle         $B/nettle/4.0/COPYING.LESSERv3
lic gmp            $B/gmp/6.3.0/COPYING.LESSERv3 $B/gmp/6.3.0/COPYING
lic libtasn1       $B/libtasn1/4.21.0/COPYING.LESSERv2
lic libidn2        $B/libidn2/2.3.8/COPYING.LESSERv3 $B/libidn2/2.3.8/COPYING.unicode
lic libunistring   $B/libunistring/1.4.2/COPYING.LIB
lic p11-kit        $B/p11-kit/0.26.5/COPYING
lic fonts          $W/deps-fonts/Caladea-OFL.txt $W/deps-fonts/Carlito-OFL.txt $W/deps-fonts/Selawik-LICENSE.txt
cp $S/../NOTICE.md $dest/
(( missing )) && exit 1
print "licenses: $(find $dest -type f | wc -l | tr -d ' ') files"
