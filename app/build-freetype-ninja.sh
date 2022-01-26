#!/bin/bash

set -eu

rm -f "$1"

src_dir=$(dirname "$0")

meson -Dbrotli=disabled \
      -Dharfbuzz=disabled \
      -Dbzip2=disabled \
      -Dpng=disabled \
      -Dtests=disabled \
      -Dzlib=disabled \
      -Dprefix=$(pwd)/freetype-install \
      freetype-build \
      "$src_dir"/../freetype \
      --cross-file=cross.txt

ninja -C freetype-build
ninja -C freetype-build install

cp freetype-build/libfreetype.so "$1"
