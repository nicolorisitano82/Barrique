#!/bin/zsh
# Packs dist/Barrique.app (made by make-dist.sh) for a GitHub release:
#   dist/release/Barrique-<version>.zip   the app, with NOTICE.md and README-alpha.txt
#   dist/release/SHA256SUMS
# GitHub refuses release files of 2 GiB or more; the script stops if the zip is
# bigger. ditto keeps the bundle's signatures, symlinks and permissions.
#
#   make-release.sh <version>      e.g. 0.1.0-alpha.1
set -e
version=${1:?version, e.g. 0.1.0-alpha.1}
W=$HOME/Work/Personale/wine
S=${0:A:h}
APP=$W/dist/Barrique.app
OUT=$W/dist/release
LIMIT=$(( 2 * 1024 * 1024 * 1024 - 1 ))

[[ -d $APP ]] || { print "no $APP: run make-dist.sh first"; exit 1 }
codesign --verify --strict $APP || { print "app signature does not verify"; exit 1 }
[[ -f $APP/Contents/Resources/licenses/NOTICE.md ]] || { print "no licenses in the app"; exit 1 }

stage=$OUT/stage-$version
rm -rf $stage; mkdir -p $stage
ditto $APP $stage/Barrique.app
cp $S/../NOTICE.md $S/../docs/README-alpha.txt $stage/
zip=$OUT/Barrique-$version.zip
rm -f $zip
(cd $stage && ditto -c -k --sequesterRsrc . $zip)
rm -rf $stage

size=$(stat -f %z $zip)
printf "%s %.2f GB\n" ${zip:t} $(( size / 1e9 ))
(( size <= LIMIT )) || { print "too big for a GitHub release file (2 GiB)"; exit 1 }
(cd $OUT && shasum -a 256 ${zip:t} > SHA256SUMS && cat SHA256SUMS)
