#!/bin/bash

set -eu

rm -f "$1"

src_dir=$(dirname "$0")

source "$src_dir/../freetype-version.sh"
FREETYPE_DIR="${src_dir}/../freetype-$FREETYPE_VERSION"

if ! test -d "$FREETYPE_DIR"; then
    echo "The freetype sources are missing from $FREETYPE_DIR" >&2
    echo "Please run the get-dependencies.sh script before building" >&2
    exit 1
fi

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
      "$FREETYPE_DIR" \
      --cross-file=cross.txt

ninja -C freetype-build
ninja -C freetype-build install

cp freetype-build/libfreetype.so "$1"
