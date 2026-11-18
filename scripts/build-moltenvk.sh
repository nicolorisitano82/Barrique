#!/bin/zsh
# Build MoltenVK (Vulkan over Metal, Apache-2.0) for Wine's win32u/winevulkan,
# macOS 15+ arm64 only, from source tarballs at the revisions MoltenVK pins in
# ExternalRevisions. No git clones and no CMake/CPM downloads: the tarballs
# are unpacked into External/ the way fetchDependencies lays out its clones,
# then the two Xcode projects are built directly.
#
#   build-moltenvk.sh <download dir> [source dir] [prefix]
#
# <download dir> holds the tarballs below (checked against SHA-256).
# Output: <prefix>/lib/libMoltenVK.dylib (install name @rpath/libMoltenVK.dylib)
# and <prefix>/include (Vulkan and MoltenVK headers), default ~/Work/Personale/wine/deps-vk.
set -e
dl=${1:?usage: build-moltenvk.sh <download dir> [source dir] [prefix]}
S=${2:-$HOME/Work/Personale/wine/deps-src/moltenvk-1.4.2}
P=${3:-$HOME/Work/Personale/wine/deps-vk}

typeset -A sums
sums=(
  MoltenVK-v1.4.2.tar.gz         6864db532f1dbbdb621a8d0ec13f24edae318fd9269dd3dd0cdff791334bb1cb
  SPIRV-Cross-6c09849f.tar.gz    b81b9956289950570953738e666a031ca32ff64e4fc925eba89f227c42109518
  SPIRV-Tools-0d6fd73c.tar.gz    20219c0962dffb0d7644bb3116a5923cfcf0526a569baa0b2a17343dd2fa6e5d
  SPIRV-Headers-29981f65.tar.gz  232899f1ad4104fb5bc377b94596c7621575eee62ad9a9e8f929b63a7dd8a7ad
  Vulkan-Headers-e3b1eec0.tar.gz f492279345cbc10708b64fcd432b3ff6c8246a5837c4db2b649abba00cf82208
  cereal-a56bad8b.tar.gz         12e10fb541270632f6683286ce8c7e8c321fe4addb53fa91ed498b95c1758f93
  volk-77689330.tar.gz           ff5b63e1b7104b34955eb4a993b94882a55f8561e971b92119069e496e3496b5
)
for f in ${(k)sums}; do
    [[ $(shasum -a 256 $dl/$f | cut -d' ' -f1) == $sums[$f] ]] || { print "checksum mismatch: $f"; exit 1; }
done

unpack() { rm -rf $2; mkdir -p $2; tar xzf $dl/$1 -C $2 --strip-components 1; }
[[ -f $S/MoltenVK.xcodeproj/project.pbxproj || -d $S/MoltenVK ]] || unpack MoltenVK-v1.4.2.tar.gz $S
E=$S/External
mkdir -p $E
unpack cereal-a56bad8b.tar.gz         $E/cereal
unpack Vulkan-Headers-e3b1eec0.tar.gz $E/Vulkan-Headers
unpack SPIRV-Cross-6c09849f.tar.gz    $E/SPIRV-Cross
unpack SPIRV-Tools-0d6fd73c.tar.gz    $E/SPIRV-Tools
unpack SPIRV-Headers-29981f65.tar.gz  $E/SPIRV-Tools/external/spirv-headers
unpack volk-77689330.tar.gz           $E/Volk
# SPIRV-Tools' generated tables, as fetchDependencies does without --build-spirv-tools
unzip -o -q -d $E/SPIRV-Tools $S/Templates/spirv-tools/build.zip
rm -rf $E/SPIRV-Tools/__MACOSX

settings=(ARCHS=arm64 ONLY_ACTIVE_ARCH=NO MACOSX_DEPLOYMENT_TARGET=15.0)
cd $S
xcodebuild -quiet -project ExternalDependencies.xcodeproj -scheme ExternalDependencies-macOS \
    -destination "generic/platform=macOS" -configuration Release \
    -derivedDataPath $S/DerivedData-deps $settings build
SKIP_PACKAGING= xcodebuild -quiet -project MoltenVKPackaging.xcodeproj -scheme "MoltenVK Package (macOS only)" \
    -destination "generic/platform=macOS" $settings build

lib=$S/Package/Release/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib
mkdir -p $P/lib $P/include
cp $lib $P/lib/
install_name_tool -id @rpath/libMoltenVK.dylib $P/lib/libMoltenVK.dylib
rsync -a --delete $E/Vulkan-Headers/include/vulkan $E/Vulkan-Headers/include/vk_video $P/include/
rsync -a --delete $S/MoltenVK/include/MoltenVK $P/include/
codesign --force --sign - $P/lib/libMoltenVK.dylib
lipo -info $P/lib/libMoltenVK.dylib
otool -L $P/lib/libMoltenVK.dylib | sed 1d
