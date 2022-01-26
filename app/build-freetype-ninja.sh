#!/bin/bash

set -eu

rm -f "$1"

src_dir=$(dirname "$0")

source "$src_dir/get_build_type.sh"

meson -Dbrotli=disabled \
      -Dharfbuzz=disabled \
      -Dbzip2=disabled \
      -Dpng=disabled \
      -Dtests=disabled \
      -Dzlib=disabled \
      -Dprefix=$(pwd)/freetype-install \
      -Dbuildtype="$build_type" \
      freetype-build \
      "$src_dir"/../freetype \
      --cross-file=cross.txt

ninja -C freetype-build
ninja -C freetype-build install

cp freetype-build/libfreetype.so "$1"
