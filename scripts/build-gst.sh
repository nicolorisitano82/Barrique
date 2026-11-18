#!/bin/zsh
# Build the media stack Wine's winegstreamer and winedmo need, LGPL only,
# into one prefix (default ~/Work/wine/deps-gst), for macOS 15+ arm64:
#   FFmpeg (decoders/demuxers, VideoToolbox, AudioToolbox, no GPL/nonfree),
#   pcre2 (static), glib, GStreamer core, plugins base/good/ugly (subset), gst-libav,
#   applemedia from plugins bad (VideoToolbox: vtenc_h264 is the H.264 encoder
#   behind Media Foundation's; it needs the GL library of plugins base).
#
#   build-gst.sh <source dir> [prefix] [stage...]
#
# <source dir> holds the extracted tarballs (ffmpeg-*, pcre2-*, glib-*,
# gstreamer-*, gst-*). Stages: ffmpeg pcre2 glib gstreamer base good ugly bad
# libav names (default: all, in that order). names rewrites every install name
# to @rpath/<lib>, so Wine's .so files find them through their deps rpath.
# pkg-config only sees the prefix and two .pc files for the SDK's libffi and
# zlib, so nothing from Homebrew is linked.
set -e
src=${1:?usage: build-gst.sh <source dir> [prefix] [stage...]}
P=${2:-$HOME/Work/wine/deps-gst}
shift $(( $# > 1 ? 2 : 1 ))
stages=(${@:-${=:-ffmpeg pcre2 glib gstreamer base good ugly bad libav names}})

export MACOSX_DEPLOYMENT_TARGET=15.0
export PKG_CONFIG_LIBDIR=$P/lib/pkgconfig PKG_CONFIG_PATH=
export CFLAGS="-O2 -mmacosx-version-min=15.0" LDFLAGS="-mmacosx-version-min=15.0 -Wl,-headerpad_max_install_names"
SDK=$(xcrun --show-sdk-path)
J=$(sysctl -n hw.ncpu)
mkdir -p $P/lib/pkgconfig

cat > $P/lib/pkgconfig/libffi.pc <<EOF
Name: libffi
Description: libffi from the macOS SDK
Version: 3.4.0
Cflags: -I$SDK/usr/include/ffi
Libs: -lffi
EOF
cat > $P/lib/pkgconfig/zlib.pc <<EOF
Name: zlib
Description: zlib from the macOS SDK
Version: 1.2.12
Cflags:
Libs: -lz
EOF

dir() { print -l $src/$1-[0-9]*(/N) | tail -1 }

# meson project: dir, then options
mesonb() {
    local d=$(dir $1); shift
    rm -rf $d/_build
    meson setup $d/_build $d --prefix=$P --libdir=lib --buildtype=release \
        -Db_ndebug=if-release -Ddefault_library=shared "$@"
    meson compile -C $d/_build -j $J
    meson install -C $d/_build --quiet
}

for s in $stages; do
    print "=== $s"
    case $s in
    ffmpeg)
        d=$(dir ffmpeg)
        ( cd $d && ./configure --prefix=$P --enable-shared --disable-static --install-name-dir='@rpath' \
            --disable-autodetect --disable-programs --disable-doc --disable-network \
            --disable-avdevice --disable-encoders --disable-muxers --disable-outdevs --disable-indevs \
            --enable-videotoolbox --enable-audiotoolbox --enable-zlib \
            --extra-cflags="$CFLAGS" --extra-ldflags="$LDFLAGS" \
          && make -j$J && make install )
        grep -q -e '--enable-gpl\|--enable-nonfree' $P/lib/pkgconfig/libavcodec.pc && { print -u2 "GPL build"; exit 1 }
        ;;
    pcre2)
        d=$(dir pcre2); rm -rf $d/_build
        cmake -S $d -B $d/_build -DCMAKE_INSTALL_PREFIX=$P -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 -DBUILD_SHARED_LIBS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
            -DPCRE2_BUILD_PCRE2GREP=OFF -DPCRE2_BUILD_TESTS=OFF -DPCRE2_SUPPORT_UNICODE=ON
        cmake --build $d/_build -j $J && cmake --install $d/_build
        ;;
    glib)
        # GLib's meson checks functions without their headers, so it finds pipe2() in
        # the SDK (macOS 27) whatever the deployment target, then calls it unguarded:
        # weakly linked, that is a jump to 0 on macOS 26. Drop it from config.h, GLib
        # falls back to pipe() + fcntl() (nm -m: pipe2 is its only post-26 function)
        d=$(dir glib)
        rm -rf $d/_build
        meson setup $d/_build $d --prefix=$P --libdir=lib --buildtype=release \
            -Db_ndebug=if-release -Ddefault_library=shared \
            -Dtests=false -Dintrospection=disabled -Ddocumentation=false -Dman-pages=disabled \
            -Dnls=disabled -Dsysprof=disabled -Dlibelf=disabled -Dselinux=disabled -Dxattr=false \
            -Ddtrace=disabled -Dsystemtap=disabled -Dglib_debug=disabled
        sed -i '' -e '/#define HAVE_PIPE2 1/d' $d/_build/config.h
        grep -q HAVE_PIPE2 $d/_build/config.h && { print -u2 "HAVE_PIPE2 still set"; exit 1 }
        meson compile -C $d/_build -j $J
        meson install -C $d/_build --quiet
        ;;
    gstreamer)
        mesonb gstreamer -Dauto_features=disabled -Dtests=disabled -Dexamples=disabled \
            -Dbenchmarks=disabled -Dtools=disabled -Ddoc=disabled -Dintrospection=disabled -Dnls=disabled \
            -Dgst_debug=true -Dcheck=disabled -Dlibunwind=disabled -Dlibdw=disabled -Dbash-completion=disabled \
            -Dcoretracers=disabled -Dgst_parse=false
        ;;
    base)
        mesonb gst-plugins-base -Dauto_features=disabled -Dtests=disabled -Dexamples=disabled -Dtools=disabled \
            -Ddoc=disabled -Dintrospection=disabled -Dnls=disabled -Dorc=disabled \
            -Dplayback=enabled -Dtypefind=enabled -Dapp=enabled -Daudioconvert=enabled -Dvideoconvertscale=enabled \
            -Daudioresample=enabled -Dvolume=enabled -Daudiorate=enabled -Dvideorate=enabled -Drawparse=enabled \
            -Dgl=enabled -Dgl_api=opengl -Dgl_platform=cgl -Dgl_winsys=cocoa \
            -Dgl-graphene=disabled -Dgl-jpeg=disabled -Dgl-png=disabled
        # only libgstgl is needed (by applemedia), not the opengl plugin
        rm -f $P/lib/gstreamer-1.0/libgstopengl.dylib
        ;;
    good)
        mesonb gst-plugins-good -Dauto_features=disabled -Dtests=disabled -Dexamples=disabled -Ddoc=disabled \
            -Dnls=disabled -Dorc=disabled \
            -Disomp4=enabled -Dmatroska=enabled -Dwavparse=enabled -Did3demux=enabled -Daudioparsers=enabled \
            -Dapetag=enabled -Davi=enabled -Dflv=enabled -Dicydemux=enabled \
            -Ddeinterlace=enabled -Dvideofilter=enabled
        ;;
    ugly)
        mesonb gst-plugins-ugly -Dauto_features=disabled -Dtests=disabled -Ddoc=disabled -Dnls=disabled \
            -Dorc=disabled -Dgpl=disabled -Dasfdemux=enabled -Drealmedia=enabled
        ;;
    bad)
        mesonb gst-plugins-bad -Dauto_features=disabled -Dtests=disabled -Dexamples=disabled -Dtools=disabled \
            -Ddoc=disabled -Dintrospection=disabled -Dnls=disabled -Dorc=disabled -Dgpl=disabled \
            -Dmpegdemux=enabled -Dvideoparsers=enabled -Dapplemedia=enabled -Dgl=enabled
        ;;
    libav)
        mesonb gst-libav -Dtests=disabled -Ddoc=disabled
        ;;
    names)
        for f in $P/lib/*.dylib(.N) $P/lib/gstreamer-1.0/*.dylib(.N); do
            id=$(otool -D $f | tail -1)
            install_name_tool -id @rpath/${id:t} $f 2>/dev/null
            for dep in $(otool -L $f | tail -n +2 | awk '{print $1}' | grep "^$P/lib/"); do
                install_name_tool -change $dep @rpath/${dep:t} $f 2>/dev/null
            done
            install_name_tool -add_rpath @loader_path -add_rpath @loader_path/.. $f 2>/dev/null || true
        done
        # gstreamer-1.0.pc adds -Wl,-rpath,$P/lib: Wine must only use its deps rpath
        sed -i '' 's/ *-Wl,-rpath,[^ ]*//g' $P/lib/pkgconfig/*.pc
        for f in $P/lib/*.dylib(.N) $P/lib/gstreamer-1.0/*.dylib(.N); do
            if otool -L $f | tail -n +2 | grep -v -e '@rpath/' -e '^\s*/usr/lib/' -e '^\s*/System/'; then
                print -u2 "${f:t}: external library path left"; exit 1
            fi
        done
        ;;
    *) print -u2 "unknown stage $s"; exit 1 ;;
    esac
done
print "media stack in $P"
