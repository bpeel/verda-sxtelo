#!/bin/bash

set -eu

srcdir=$(cd $(dirname "$0") && pwd)
source "$srcdir/freetype-version.sh"

prefix="$srcdir/ios-app/Grabagram/external"

debug_cross_file="$srcdir/"ios-debug-cross.txt
release_cross_file="$srcdir/"ios-release-cross.txt

freetype_dir="$srcdir/freetype-$FREETYPE_VERSION"

android_assets="$srcdir/app/src/main/assets"
ios_assets="$srcdir/ios-app/Grabagram/Assets.xcassets/assets"

function generate_cross_file {
        local cross_file="$1"; shift
        local platform="$1"; shift
        local cpu_family="$1"; shift
        local archs=("$@")
        
        local dev_root="/Applications/Xcode.app/Contents/Developer/Platforms/$platform.platform/Developer"
        local sdk_root="$dev_root/SDKs/$platform.sdk"
        local minarg="-m"$(echo "$platform" | tr '[:upper:]' '[:lower:]')"-version-min"
        local minver="13.1"

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
                echo -n "$var""_args = [" >> "$cross_file"
                for arch in "${archs[@]}"; do
                        echo -n "'-arch', '$arch', " >> "$cross_file"
                done
                
                echo "'$minarg=$minver'," \
                     "'-isysroot', '$sdk_root']" >> "$cross_file"
        done

        cat >> "$cross_file" <<EOF

[properties]
root = '$dev_root'
has_function_printf = true
has_function_hfkerhisadf = false

[host_machine]
system = 'darwin'
cpu_family = '$cpu_family'
cpu = '${archs[0]}'
endian = 'little'
EOF
}

function build_freetype {
        local suffix="$1"; shift
        local cross_file="$1"; shift
       
        meson \
                "$freetype_dir"{/build"$suffix",} \
                --cross-file="$cross_file" \
                -Dbrotli=disabled \
                -Dharfbuzz=disabled \
                -Dbzip2=disabled \
                -Dpng=disabled \
                -Dtests=disabled \
                -Dzlib=disabled \
                -Dprefix="$prefix$suffix" \
                -Ddefault_library=static \
                -Dbuildtype=release

        meson install -C "$freetype_dir/build$suffix"
}

function build_lib {
        local suffix="$1"; shift
        local cross_file="$1"; shift
        local build_type="$1"; shift
        
        meson \
                "$srcdir"{/client-ios-build"$suffix",} \
                --cross-file="$cross_file" \
                -Dclient=false -Dclientlib=true -Dserver=false \
                -Dprefix="$prefix$suffix" \
                -Dbuildtype="$build_type"

        meson install -C "$srcdir/client-ios-build$suffix"
}

function build_all {
        local suffix="$1"; shift
        local cross_file="$1"; shift
        local build_type="$1"; shift

        export PKG_CONFIG_PATH="$prefix$suffix/lib/pkgconfig"

        build_freetype "$suffix" "$cross_file"
        build_lib "$suffix" "$cross_file" "$build_type"
}

generate_cross_file "$debug_cross_file" "iPhoneSimulator" "x86_64" "x86_64"
generate_cross_file "$release_cross_file" "iPhoneOS" "arm" "arm64"

build_all "" "$debug_cross_file" "debug"
build_all "-release" "$release_cross_file" "release"

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
