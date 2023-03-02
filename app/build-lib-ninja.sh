#!/bin/bash

set -eu

rm -f "$1"

src_dir=$(dirname "$0")

source "$src_dir/get_build_type.sh"

export PKG_CONFIG_LIBDIR=$(pwd)/freetype-install/lib/pkgconfig

meson -Djni=true -Dclient=false -Dserver=false \
      -Dbuildtype="$build_type" \
      lib-build \
      "$src_dir"/.. \
      --cross-file=cross.txt

ninja -C lib-build

cp lib-build/client/libanagrams.so "$1"
