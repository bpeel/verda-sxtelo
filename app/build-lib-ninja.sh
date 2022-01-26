#!/bin/bash

set -eu

rm -f "$1"

src_dir=$(dirname "$0")

export PKG_CONFIG_LIBDIR=$(pwd)/freetype-install/lib/pkgconfig

meson -Djni=true -Dclient=false -Dserver=false \
      lib-build \
      "$src_dir"/.. \
      --cross-file=cross.txt

ninja -C lib-build

cp lib-build/client/libanagrams.so "$1"
