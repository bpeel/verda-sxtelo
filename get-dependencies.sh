#!/bin/bash

set -eu

src_dir=$(dirname "$0")

source "$src_dir/freetype-version.sh"

FREETYPE_DIR="${src_dir}/freetype-$FREETYPE_VERSION"
FREETYPE_TAR="${src_dir}/freetype-$FREETYPE_VERSION.tar.xz"
FREETYPE_URL="https://download.savannah.gnu.org/releases/freetype/$FREETYPE_TAR"
FREETYPE_HASH="3333ae7cfda88429c97a7ae63b7d01ab398076c3b67182e960e5684050f2c5c8"

if which shasum > /dev/null; then
    shasum="shasum -a 256 -c -"
else
    shasum="sha256sum --check"
fi

if ! test -d "$FREETYPE_DIR"; then
    if ! test -f "$FREETYPE_TAR"; then
        curl --location --output "$FREETYPE_TAR" "$FREETYPE_URL"
    fi

    echo "$FREETYPE_HASH  $FREETYPE_TAR" | $shasum
    tar --directory="$src_dir" --extract --file="$FREETYPE_TAR"
fi
