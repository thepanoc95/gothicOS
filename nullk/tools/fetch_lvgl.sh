#!/bin/sh
# Fetch the pinned LVGL release used by null_kernel's LVGL port.
#
# LVGL is ~19 MiB of source, so it is staged into build/third_party instead of
# being committed. The version and tarball checksum are pinned here; every
# build that needs LVGL calls this script first.
#
# Usage: nullk/tools/fetch_lvgl.sh [--print-dir]
set -eu

LVGL_VERSION="9.2.2"
LVGL_SHA256="129b4e00e06639fa79d7e8a6cab3c1ecce2445b1a246652ccd34f22e7b17ad6f"
LVGL_URL="https://github.com/lvgl/lvgl/archive/refs/tags/v${LVGL_VERSION}.tar.gz"

TOOLS_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
NULLK_DIR=$(CDPATH= cd -- "$TOOLS_DIR/.." && pwd)
BUILD_DIR=$(CDPATH= cd -- "$NULLK_DIR/.." && pwd)/build
DEST="$BUILD_DIR/third_party/lvgl-$LVGL_VERSION"

if [ "${1:-}" = "--print-dir" ]; then
    printf '%s\n' "$DEST"
    exit 0
fi

if [ -f "$DEST/lvgl.h" ] && [ -f "$DEST/lv_version.h" ]; then
    printf '%s\n' "$DEST"
    exit 0
fi

mkdir -p "$BUILD_DIR/third_party"
TARBALL="$BUILD_DIR/third_party/lvgl-$LVGL_VERSION.tar.gz"

if [ ! -f "$TARBALL" ]; then
    echo "fetch_lvgl: downloading LVGL $LVGL_VERSION" >&2
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL -o "$TARBALL.part" "$LVGL_URL"
    elif command -v wget >/dev/null 2>&1; then
        wget -qO "$TARBALL.part" "$LVGL_URL"
    else
        echo "fetch_lvgl: curl or wget is required" >&2
        exit 1
    fi
    mv "$TARBALL.part" "$TARBALL"
fi

if command -v sha256sum >/dev/null 2>&1; then
    ACTUAL=$(sha256sum "$TARBALL" | cut -d' ' -f1)
elif command -v shasum >/dev/null 2>&1; then
    ACTUAL=$(shasum -a 256 "$TARBALL" | cut -d' ' -f1)
else
    ACTUAL=""
fi

if [ -n "$ACTUAL" ] && [ "$ACTUAL" != "$LVGL_SHA256" ]; then
    echo "fetch_lvgl: checksum mismatch for $TARBALL" >&2
    echo "  expected $LVGL_SHA256" >&2
    echo "  actual   $ACTUAL" >&2
    rm -f "$TARBALL"
    exit 1
fi

echo "fetch_lvgl: extracting LVGL $LVGL_VERSION" >&2
STAGE="$BUILD_DIR/third_party/.lvgl-stage"
rm -rf "$STAGE"
mkdir -p "$STAGE"
tar -xzf "$TARBALL" -C "$STAGE"

rm -rf "$DEST"
mkdir -p "$DEST"
mv "$STAGE/lvgl-$LVGL_VERSION/src" "$DEST/src"
mv "$STAGE/lvgl-$LVGL_VERSION/lvgl.h" "$DEST/lvgl.h"
mv "$STAGE/lvgl-$LVGL_VERSION/lv_version.h" "$DEST/lv_version.h"
mv "$STAGE/lvgl-$LVGL_VERSION/LICENCE.txt" "$DEST/LICENCE.txt"
rm -rf "$STAGE"

printf '%s\n' "$DEST"
