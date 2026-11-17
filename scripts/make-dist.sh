#!/bin/zsh
# Build Barrique.app (phase 6): one self-contained bundle, nothing installed
# elsewhere. Layout:
#   Barrique.app/Contents/Info.plist
#   Barrique.app/Contents/MacOS/barrique             launcher (see barrique-launcher.sh)
#   Barrique.app/Contents/Resources/AppIcon.icns     from resources/icon.svg
#   Barrique.app/Contents/Resources/wine/bin/        wine (starts the loader), wineserver
#   (the loader, lib/wine/aarch64-unix/wine, is signed with the hypervisor entitlement)
#   Barrique.app/Contents/Resources/wine/lib/wine/   aarch64-unix, aarch64-windows (+ FEX), i386-windows,
#                                                    dxmt/ (BARRIQUE_D3D=dxmt), winemetal.so in aarch64-unix
#   Barrique.app/Contents/Resources/wine/lib/deps/   FreeType, GStreamer/FFmpeg, MoltenVK (@rpath)
#   Barrique.app/Contents/Resources/wine/share/wine/ wine.inf, nls, fonts (+ Selawik/Carlito/Caladea)
#
#   make-dist.sh [app path] [wine build dir]
#
# Default ~/Work/wine/dist/Barrique.app from ~/Work/wine/build-arm64ec. Does not
# run Wine. Debug information is stripped; every Mach-O is signed ad hoc.
set -e
W=$HOME/Work/wine
APP=${1:-$W/dist/Barrique.app}
WINE_BUILD=${2:-$W/build-arm64ec}
S=${0:A:h}
R=$APP/Contents/Resources/wine
LIB=$R/lib/wine
MINGW=$W/llvm-mingw/bin

rm -rf $APP
mkdir -p $APP/Contents/MacOS $R

print "== Wine"
make -C $WINE_BUILD -j10 install-lib prefix=$R > $W/dist/install.log 2>&1

print "== libraries and fonts"
zsh $S/bundle-deps.sh $R/lib

print "== FEX"
cp -L $W/fex-dlls/aarch64-windows/libarm64ecfex.dll $W/fex-dlls/aarch64-windows/libwow64fex.dll $LIB/aarch64-windows/

print "== DXMT"
mkdir -p $LIB/dxmt
cp -R $W/deps-dxmt/dxmt/aarch64-windows $W/deps-dxmt/dxmt/x86_64-windows $LIB/dxmt/
cp $W/deps-dxmt/dxmt/aarch64-unix/winemetal.so $LIB/aarch64-unix/   # next to winemac.so (@loader_path)

print "== strip"
find $LIB -type f \( -name "*.dll" -o -name "*.exe" -o -name "*.drv" -o -name "*.sys" -o -name "*.ocx" \
     -o -name "*.cpl" -o -name "*.acm" -o -name "*.ax" -o -name "*.tlb" -o -name "*.ds" -o -name "*.msstyles" \) \
     -exec $MINGW/llvm-strip --strip-debug {} + 2>/dev/null || true
find $R -type f | while read f; do
    [[ $(file -b "$f") == Mach-O* ]] && strip -S -x "$f" 2>/dev/null || true
done

print "== app"
mkdir -p $R/share/barrique
cp $S/prefix-defaults.reg $R/share/barrique/
install -m 755 $S/barrique-launcher.sh $APP/Contents/MacOS/barrique
# icon: resources/icon.svg rendered at every size macOS wants, then iconutil
iconset=$W/dist/AppIcon.iconset
rm -rf $iconset; mkdir -p $iconset $APP/Contents/Resources
for size in 16 32 128 256 512; do
    rsvg-convert -w $size -h $size $S/../resources/icon.svg -o $iconset/icon_${size}x${size}.png
    rsvg-convert -w $((size * 2)) -h $((size * 2)) $S/../resources/icon.svg -o $iconset/icon_${size}x${size}@2x.png
done
iconutil -c icns -o $APP/Contents/Resources/AppIcon.icns $iconset
rm -rf $iconset
version=$(git -C $S/.. describe --always --dirty 2>/dev/null || print dev)
cat > $APP/Contents/Info.plist <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>                 <string>Barrique</string>
    <key>CFBundleDisplayName</key>          <string>Barrique</string>
    <key>CFBundleIdentifier</key>           <string>org.barrique.Barrique</string>
    <key>CFBundleExecutable</key>           <string>barrique</string>
    <key>CFBundleIconFile</key>             <string>AppIcon</string>
    <key>CFBundlePackageType</key>          <string>APPL</string>
    <key>CFBundleShortVersionString</key>   <string>0.1</string>
    <key>CFBundleVersion</key>              <string>$version</string>
    <key>LSMinimumSystemVersion</key>       <string>26.0</string>
    <key>LSArchitecturePriority</key>       <array><string>arm64</string></array>
    <key>NSHighResolutionCapable</key>      <true/>
    <key>NSMicrophoneUsageDescription</key> <string>Windows programs that record audio</string>
</dict>
</plist>
PLIST

print "== sign"
find $R -type f | while read f; do
    [[ $(file -b "$f") == Mach-O* ]] && codesign --force --sign - "$f" 2>/dev/null
done
# the loader that creates the VM is lib/wine/aarch64-unix/wine (bin/wine only finds and starts it)
codesign --force --sign - --entitlements $S/../entitlements.plist $LIB/aarch64-unix/wine
codesign --force --sign - $APP   # the bundle itself, after everything inside (no --deep: keeps wine's entitlement)

print "== check Mach-O"
bad=0
find $R -type f | while read f; do
    [[ $(file -b "$f") == Mach-O* ]] || continue
    if [[ $(lipo -archs "$f" 2>/dev/null) != arm64 ]]; then print "not arm64-only: $f"; bad=1; fi
    minos=$(otool -l "$f" | awk '/LC_BUILD_VERSION/{b=1} b&&/minos/{print $2; exit}')
    if [[ -n $minos && ${minos%%.*} -gt 26 ]]; then print "minos $minos: $f"; bad=1; fi
    if otool -L "$f" | tail -n +2 | grep -q -e /opt/homebrew -e /usr/local -e $HOME; then
        print "external library path: $f"; otool -L "$f" | grep -e /opt/homebrew -e /usr/local -e $HOME; bad=1
    fi
done
codesign -d --entitlements - $LIB/aarch64-unix/wine 2>/dev/null | grep -q com.apple.security.hypervisor || { print "the loader is not signed with the hypervisor entitlement"; bad=1; }
codesign --verify --strict $APP 2>/dev/null || { print "bundle signature does not verify"; codesign --verify --strict $APP; bad=1; }
du -sh $APP
print "check: $([[ $bad == 0 ]] && print ok || print FAILED)"
