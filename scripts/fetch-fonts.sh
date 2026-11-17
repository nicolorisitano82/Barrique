#!/bin/zsh
# Download the free fonts with Windows metrics that Barrique bundles
# (phase 5 step 1b) and check them against pinned SHA-256 sums:
#   Selawik 1.01 (Microsoft, OFL)    -> Segoe UI metrics
#   Carlito (google/fonts, OFL)      -> Calibri metrics
#   Caladea (google/fonts, OFL)      -> Cambria metrics
# The fonts are not modified (Selawik is a Reserved Font Name): Windows names
# point at them through font replacements (wine.inf, win32u/font.c).
#
#   fetch-fonts.sh <dir>     -> <dir>/*.ttf and the licenses
set -e
d=${1:?usage: fetch-fonts.sh <dir>}; mkdir -p $d; cd $d
GF=https://raw.githubusercontent.com/google/fonts/7af0d90b4d532ffa4b041937b560c6df4e7b1e95/ofl

fetch() { [[ -f $2 ]] || curl -fsSL -o $2 $1 }

fetch https://github.com/microsoft/Selawik/releases/download/1.01/Selawik_Release.zip Selawik_Release.zip
fetch https://raw.githubusercontent.com/microsoft/Selawik/1.01/LICENSE.txt Selawik-LICENSE.txt
for s in Regular Bold Italic BoldItalic; do
    fetch $GF/carlito/Carlito-$s.ttf Carlito-$s.ttf
    fetch $GF/caladea/Caladea-$s.ttf Caladea-$s.ttf
done
fetch $GF/carlito/OFL.txt Carlito-OFL.txt
fetch $GF/caladea/OFL.txt Caladea-OFL.txt

shasum -a 256 -c <<'EOF'
3f62c51e05e3b5a1e6241cf92a371f0be2ea1183aa87b30718bbd40832a8d423  Selawik_Release.zip
bb5d20f79b82599ec72983597437373a80f2d2085fa91fc144fd74e876a594db  Carlito-Bold.ttf
b32928186c119599e03ca6a1ffc680fdcb7fac95772f4b95d989cf6cd3861517  Carlito-BoldItalic.ttf
0b019225e58d702bfedcbd35c21696769f8ee115cb6343f84c2f240312450d1c  Carlito-Italic.ttf
f6418f708baede9789daef5d458c0f53d2a888af9820e8062934e504fedc6595  Carlito-Regular.ttf
ae3cb2dcbc925809dd29d2a44e9802211cab66be541bacbfc9c08c74b27c3742  Caladea-Bold.ttf
ccabaa7b7e2fdf253d2b1a5fa699dd8a3df8d835a9eb285ad82631a677eb76c0  Caladea-BoldItalic.ttf
4359a8e24f748b6447b1ff6d7a174febe70961d29f8bb8634b56dacd740a3deb  Caladea-Italic.ttf
f1e899278b7b4491aba5b6a8253c4b04c050cc59b21865be5c37559a775153cd  Caladea-Regular.ttf
58402f82a7c332a700294988fe7554fbb0a63a8d27ccc1ee3bbc640311990a00  Carlito-OFL.txt
ccdab61d371d8c8683a128a92cd7d498dbdb1d37689f7cb21f1bf6b16658d213  Caladea-OFL.txt
EOF
unzip -o -q -j Selawik_Release.zip '*.ttf' -d .
print "fonts in $d: $(ls *.ttf | wc -l | tr -d ' ') files"
