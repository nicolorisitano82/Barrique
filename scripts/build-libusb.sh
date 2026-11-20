#!/bin/zsh
# Build libusb (LGPL 2.1) for wineusb.sys: Windows USB drivers and programs reach
# USB devices that no macOS driver has claimed. Homebrew's libusb is built for a
# newer macOS, so it is built here: arm64, macOS 26, install name @rpath/.
#
#   build-libusb.sh [source dir] [prefix]
#
# Default ~/Work/Personale/wine/deps-src/libusb-1.0.30 into ~/Work/Personale/wine/deps-usb;
# Wine is configured with USB_CFLAGS/USB_LIBS pointing there, and bundle-deps.sh
# copies lib/libusb-1.0.0.dylib next to the other libraries.
set -e
S=${1:-$HOME/Work/Personale/wine/deps-src/libusb-1.0.30}
P=${2:-$HOME/Work/Personale/wine/deps-usb}
unset CFLAGS CXXFLAGS CPPFLAGS LDFLAGS PKG_CONFIG_PATH   # nothing from the user's shell (Homebrew paths)
cd $S
[[ -f Makefile ]] && make distclean >/dev/null 2>&1 || true
# pipe2 is in the SDK but only exists from macOS 27: it would be left weak and
# jump to 0 on macOS 26 (same as in ntdll)
./configure --prefix=$P --disable-static --enable-shared --disable-examples-build --disable-tests-build \
    ac_cv_func_pipe2=no CC=clang CFLAGS="-O2 -arch arm64 -mmacosx-version-min=26.0" LDFLAGS="-arch arm64 -mmacosx-version-min=26.0"
make -j8
make install
install_name_tool -id @rpath/libusb-1.0.0.dylib $P/lib/libusb-1.0.0.dylib
otool -L $P/lib/libusb-1.0.0.dylib
otool -l $P/lib/libusb-1.0.0.dylib | awk '/LC_BUILD_VERSION/{b=1} b&&/minos/{print "minos", $2; exit}'
