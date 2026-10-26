#!/bin/bash
# Builds SDL2 and SDL2_ttf (shared libraries; SDL2_ttf with its vendored FreeType, no HarfBuzz)
# from the release sources into PREFIX, for the Linux and macOS packages (release.yml). The
# licence files go to PREFIX/licenses/SDL2-*/ and PREFIX/licenses/SDL2_ttf-*/ (package.sh).
#
#   packaging/build_sdl.sh PREFIX [CMAKE_ARGS...]     e.g. -DCMAKE_OSX_ARCHITECTURES=arm64
set -euo pipefail
SDL2_VERSION=2.30.11
SDL2_SHA256=8b8d4aef2038533da814965220f88f77d60dfa0f32685f80ead65e501337da7f
TTF_VERSION=2.24.0
TTF_SHA256=0b2bf1e7b6568adbdbc9bb924643f79d9dedafe061fa1ed687d1d9ac4e453bfd
prefix=$1
shift
mkdir -p "$prefix"
prefix="$(cd "$prefix" && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

fetch() {  # fetch URL SHA256 : downloads and unpacks a source tarball into $work
    curl -fsSL -o "$work/src.tar.gz" "$1"
    echo "$2  $work/src.tar.gz" | shasum -a 256 -c - >/dev/null || { echo "checksum mismatch: $1" >&2; exit 1; }
    tar xzf "$work/src.tar.gz" -C "$work"
    rm "$work/src.tar.gz"
}
fetch "https://github.com/libsdl-org/SDL/releases/download/release-$SDL2_VERSION/SDL2-$SDL2_VERSION.tar.gz" "$SDL2_SHA256"
fetch "https://github.com/libsdl-org/SDL_ttf/releases/download/release-$TTF_VERSION/SDL2_ttf-$TTF_VERSION.tar.gz" "$TTF_SHA256"

jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
common=(-DCMAKE_BUILD_TYPE=Release "-DCMAKE_INSTALL_PREFIX=$prefix" "-DCMAKE_PREFIX_PATH=$prefix"
        -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_POLICY_VERSION_MINIMUM=3.5 "$@")
cmake -S "$work/SDL2-$SDL2_VERSION" -B "$work/build-sdl" "${common[@]}" \
    -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST=OFF -DSDL_TESTS=OFF
cmake --build "$work/build-sdl" --parallel "$jobs"
cmake --install "$work/build-sdl"
cmake -S "$work/SDL2_ttf-$TTF_VERSION" -B "$work/build-ttf" "${common[@]}" \
    -DBUILD_SHARED_LIBS=ON -DSDL2TTF_VENDORED=ON -DSDL2TTF_HARFBUZZ=OFF -DSDL2TTF_SAMPLES=OFF
cmake --build "$work/build-ttf" --parallel "$jobs"
cmake --install "$work/build-ttf"

lic="$prefix/licenses"
mkdir -p "$lic/SDL2-$SDL2_VERSION" "$lic/SDL2_ttf-$TTF_VERSION/external/freetype/docs"
cp "$work/SDL2-$SDL2_VERSION/LICENSE.txt" "$lic/SDL2-$SDL2_VERSION/"
cp "$work/SDL2_ttf-$TTF_VERSION/LICENSE.txt" "$lic/SDL2_ttf-$TTF_VERSION/"
cp "$work/SDL2_ttf-$TTF_VERSION/external/freetype/LICENSE.TXT" "$lic/SDL2_ttf-$TTF_VERSION/external/freetype/"
cp "$work/SDL2_ttf-$TTF_VERSION/external/freetype/docs/FTL.TXT" "$lic/SDL2_ttf-$TTF_VERSION/external/freetype/docs/"
