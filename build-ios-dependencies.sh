#!/bin/bash

set -eu

srcdir=$(cd $(dirname "$0") && pwd)
source "$srcdir/freetype-version.sh"

prefix="$srcdir/ios-app/Grabagram/external"

cross_file="$srcdir/"ios-cross.txt

dev_root="/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneSimulator.platform/Developer"
sdk_root="$dev_root/SDKs/iPhoneSimulator.sdk"
arch="x86_64"
minarg="-miphonesimulator-version-min"
minver="11.0"
freetype_dir="$srcdir/freetype-$FREETYPE_VERSION"

android_assets="$srcdir/app/src/main/assets"
ios_assets="$srcdir/ios-app/Grabagram/Assets.xcassets/assets"

cat > "$cross_file" <<"EOF"
[binaries]
c = 'clang'
cpp = 'clang++'
objc = 'clang'
objcpp = 'clang++'
ar = 'ar'
strip = 'strip'
pkgconfig = 'pkg-config'

[built-in options]
EOF

for var in {c,cpp}{,_link}; do
  echo "$var""_args = ['-arch', '$arch', '$minarg=$minver', '-isysroot', '$sdk_root']" >> "$cross_file"
done

cat >> "$cross_file" <<EOF

[properties]
root = '$dev_root'
has_function_printf = true
has_function_hfkerhisadf = false

[host_machine]
system = 'darwin'
cpu_family = '$arch'
cpu = '$arch'
endian = 'little'
EOF

meson \
 "$freetype_dir"{/build,} \
 --cross-file="$cross_file" \
 -Dbrotli=disabled \
 -Dharfbuzz=disabled \
 -Dbzip2=disabled \
 -Dpng=disabled \
 -Dtests=disabled \
 -Dzlib=disabled \
 -Dprefix="$prefix" \
 -Ddefault_library=static \
 -Dbuildtype=release

meson install -C "$freetype_dir/build"

export PKG_CONFIG_PATH="$prefix/lib/pkgconfig"

meson \
 "$srcdir"{/client-ios-build,} \
 --cross-file="$cross_file" \
 -Dclient=false -Dclientlib=true -Dserver=false \
 -Dprefix="$prefix" \
 -Dbuildtype=debug

meson install -C "$srcdir/client-ios-build"

for file in "$android_assets/"*; do
    if echo "$file" | grep -q '~$'; then
        continue
    fi

    bn=$(basename "$file")
    bn_noext=$(echo "$bn" | sed -E 's/\.[^\.]+$//')
    dir="$ios_assets/$bn_noext".dataset
    mkdir -p "$dir"
    cp "$file" "$dir/$bn"
    cat > "$dir/Contents.json" <<EOF
{
 "data" : [
  {
   "filename" : "$bn",
   "idiom" : "universal"
  }
 ],
 "info" : {
  "author" : "xcode",
  "version" : 1
 }
}
EOF
done
