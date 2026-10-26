#!/bin/bash
# Assembles a release package of smacx (used by .github/workflows/release.yml).
#
#   packaging/package.sh PLATFORM BUILD_DIR SDL_LIB_DIR VERSION OUT_DIR [LICENSE_DIR...]
#
# PLATFORM: linux-x86_64 (a .tar.gz: smacx, lib/ with the SDL2 shared libraries, found through the
# executable's RUNPATH $ORIGIN/lib), macos-arm64 / macos-universal (a .zip with smacx.app: the
# SDL2 dylibs in Contents/Frameworks, @rpath, ad-hoc signed) or windows-x86_64 / windows-i686
# (a .zip: smacx.exe next to SDL2.dll and SDL2_ttf.dll). SDL_LIB_DIR holds the SDL2 / SDL2_ttf
# libraries (lib/ of an SDL prefix; for Windows the bin/ folders of the two mingw development
# packages, separated by ':'). LICENSE_DIRs are
# source or development package directories of SDL2 and SDL2_ttf: their licence files go to
# licenses/. Every package has the default ini, the players' README and the licence.
# The package never contains game data: the files are checked against an allow-list at the end.
set -euo pipefail
platform=$1 build=$2 sdllib=$3 version=$4 out=$5
shift 5
root="$(cd "$(dirname "$0")/.." && pwd)"
name="smacx-$version-$platform"
mkdir -p "$out"
out="$(cd "$out" && pwd)"
stage="$out/$name"
rm -rf "$stage"
mkdir -p "$stage/licenses"

cp "$root/packaging/README.txt" "$stage/README.txt"
cp "$root/LICENSE" "$stage/LICENSE.txt"
for dir in "$@"; do
    base="$(basename "$dir")"
    for f in LICENSE.txt COPYING.txt; do
        [ -f "$dir/$f" ] && cp "$dir/$f" "$stage/licenses/${base%%-[0-9]*}.txt"
    done
    [ -f "$dir/external/freetype/LICENSE.TXT" ] && cp "$dir/external/freetype/LICENSE.TXT" "$stage/licenses/freetype.txt"
    [ -f "$dir/external/freetype/docs/FTL.TXT" ] && cp "$dir/external/freetype/docs/FTL.TXT" "$stage/licenses/freetype-FTL.txt"
    [ -f "$dir/external/harfbuzz/COPYING" ] && cp "$dir/external/harfbuzz/COPYING" "$stage/licenses/harfbuzz.txt"
done
ini="$root/harness/oracle/runtime/Alpha Centauri.Ini"
lib() {  # lib NAME : the library NAME in one of the SDL_LIB_DIR folders
    local d
    IFS=: read -ra dirs <<< "$sdllib"
    for d in "${dirs[@]}"; do
        [ -e "$d/$1" ] && { echo "$d/$1"; return; }
    done
    echo "$1 not found in $sdllib" >&2
    exit 1
}

case "$platform" in
linux-*)
    mkdir -p "$stage/lib"
    cp "$build/smacx" "$stage/smacx"
    strip "$stage/smacx"
    for lib in libSDL2-2.0.so.0 libSDL2_ttf-2.0.so.0; do
        cp -L "$(lib "$lib")" "$stage/lib/$lib"
        strip --strip-unneeded "$stage/lib/$lib"
    done
    cp "$ini" "$stage/"
    readelf -d "$stage/smacx" | grep -q 'R.*PATH.*\$ORIGIN/lib' || { echo "smacx has no \$ORIGIN/lib RUNPATH" >&2; exit 1; }
    ;;
macos-*)
    app="$stage/smacx.app/Contents"
    mkdir -p "$app/MacOS" "$app/Frameworks" "$app/Resources"
    cp "$build/smacx" "$app/MacOS/smacx"
    cp "$ini" "$app/Resources/"
    sed "s/@VERSION@/${version#v}/" "$root/packaging/Info.plist" > "$app/Info.plist"
    for lib in libSDL2-2.0.0.dylib libSDL2_ttf-2.0.0.dylib; do
        cp -L "$(lib "$lib")" "$app/Frameworks/$lib"
        chmod u+w "$app/Frameworks/$lib"
        install_name_tool -id "@rpath/$lib" "$app/Frameworks/$lib"
    done
    # Every reference to an SDL library goes through @rpath = Contents/Frameworks.
    for bin in "$app/MacOS/smacx" "$app/Frameworks/"*.dylib; do
        otool -L "$bin" | tail -n +2 | awk '{print $1}' | grep -E 'libSDL2(_ttf)?-2\.0\.0\.dylib$' | while read -r dep; do
            [ "$dep" = "@rpath/$(basename "$dep")" ] || install_name_tool -change "$dep" "@rpath/$(basename "$dep")" "$bin"
        done
    done
    otool -l "$app/MacOS/smacx" | awk '$1 == "cmd" && $2 == "LC_RPATH" {getline; getline; print $2}' | while read -r rp; do
        install_name_tool -delete_rpath "$rp" "$app/MacOS/smacx"
    done
    install_name_tool -add_rpath "@executable_path/../Frameworks" "$app/MacOS/smacx"
    strip -x "$app/MacOS/smacx"
    codesign --force --sign - "$app/Frameworks/"*.dylib
    codesign --force --sign - "$stage/smacx.app"
    codesign --verify --deep --strict "$stage/smacx.app"
    otool -L "$app/MacOS/smacx"
    ;;
windows-*)
    cp "$build/smacx.exe" "$stage/smacx.exe"
    cp "$(lib SDL2.dll)" "$(lib SDL2_ttf.dll)" "$stage/"
    cp "$ini" "$stage/"
    ;;
*)
    echo "unknown platform $platform" >&2
    exit 2
    ;;
esac

# No game data: only these files may be in a package.
bad=$(cd "$stage" && find . -type f | sed 's|^\./||' | grep -vE '^(smacx|smacx\.exe|SDL2(_ttf)?\.dll|lib/libSDL2(_ttf)?-2\.0\.so\.0|Alpha Centauri\.Ini|README\.txt|LICENSE\.txt|licenses/[A-Za-z0-9_.-]+\.txt|smacx\.app/Contents/(Info\.plist|MacOS/smacx|Frameworks/libSDL2(_ttf)?-2\.0\.0\.dylib|Resources/Alpha Centauri\.Ini|_CodeSignature/CodeResources))$' || true)
if [ -n "$bad" ]; then
    echo "unexpected files in the package:" >&2
    echo "$bad" >&2
    exit 1
fi

cd "$out"
case "$platform" in
linux-*) tar czf "$name.tar.gz" "$name" && echo "$out/$name.tar.gz" ;;
macos-*) ditto -c -k --keepParent "$name" "$name.zip" && echo "$out/$name.zip" ;;
*) python3 -m zipfile -c "$name.zip" "$name" && echo "$out/$name.zip" ;;
esac
