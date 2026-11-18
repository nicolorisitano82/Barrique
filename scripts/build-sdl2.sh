#!/bin/zsh
# Build SDL2 (zlib license) for winebus.sys's SDL backend: game controllers with
# SDL's mapping database (XInput layout for hundreds of pads), better than the
# plain IOHID backend. Homebrew's libSDL2 is sdl2-compat over SDL3 and needs
# macOS 27, so it is built here: arm64, macOS 26, only what winebus uses
# (joystick, game controller, haptic, HIDAPI), install name @rpath/.
#
#   build-sdl2.sh [source dir] [prefix]
#
# Default ~/Work/Personale/wine/deps-src/SDL2-2.32.10 into ~/Work/Personale/wine/deps-sdl2;
# bundle-deps.sh copies lib/libSDL2-2.0.0.dylib next to the other libraries.
set -e
S=${1:-$HOME/Work/Personale/wine/deps-src/SDL2-2.32.10}
P=${2:-$HOME/Work/Personale/wine/deps-sdl2}
B=$S/_build
unset CFLAGS CXXFLAGS CPPFLAGS LDFLAGS   # nothing from the user's shell (Homebrew paths)
rm -rf $B
cmake -S $S -B $B -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$P \
    -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0 \
    -DCMAKE_INSTALL_NAME_DIR=@rpath -DCMAKE_FIND_FRAMEWORK=LAST \
    -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST=OFF -DSDL2_DISABLE_SDL2MAIN=ON \
    -DSDL_JOYSTICK=ON -DSDL_HAPTIC=ON -DSDL_HIDAPI=ON -DSDL_HIDAPI_JOYSTICK=ON \
    -DSDL_AUDIO=OFF -DSDL_VIDEO=OFF -DSDL_RENDER=OFF -DSDL_SENSOR=ON -DSDL_POWER=OFF \
    -DSDL_FILESYSTEM=OFF -DSDL_LOCALE=OFF -DSDL_MISC=OFF -DSDL_TIMERS=ON -DSDL_EVENTS=ON
cmake --build $B
cmake --install $B
otool -L $P/lib/libSDL2-2.0.0.dylib
otool -l $P/lib/libSDL2-2.0.0.dylib | awk '/LC_BUILD_VERSION/{b=1} b&&/minos/{print "minos", $2; exit}'
