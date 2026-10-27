# Sourced by the scripts that run the original under Wine (run_oracle.sh, tests/popups/orig_drive.py
# through `bash -c`): the prefix, and the fonts and text rendering of a Windows installation.
#
# * The game's fonts: the installer puts Arial Narrow (arialn*.ttf) and the "Alpha Centauri" face
#   (ALPHC___.TTF) into the system's fonts. terranx.exe itself only adds arialn.ttf (WinMain:
#   Font_init_2's CreateScalableFontResource / AddFontResource), so without them Wine draws bold
#   Arial Narrow by emboldening the regular face and the title screen's credits in a substitute.
#   They are copied into the prefix's Fonts folder and registered once.
# * FreeType's v35 TrueType interpreter: Wine's GetTextExtentPoint32 then sums the hinted advances
#   (the fonts' hdmx widths), the widths Windows measures with and Wine's TextOut draws with on the
#   game's 8-bit DIBs. With the default v40 interpreter Wine measures unhinted advances, a text
#   is measured narrower or wider than it is drawn, and wrapped lines and centred text move by a
#   pixel or more against Windows.
# ROOT must be the repository root.
export WINEPREFIX="$ROOT/work/wineprefix" WINEDEBUG=-all
export FREETYPE_PROPERTIES="truetype:interpreter-version=35"
wine_install_fonts() {
    local fonts="$WINEPREFIX/drive_c/windows/Fonts" key='HKLM\Software\Microsoft\Windows NT\CurrentVersion\Fonts'
    local f name missing=0
    for f in arialn.ttf arialnb.ttf arialni.ttf arialnbi.ttf ALPHC___.TTF; do
        [ -f "$fonts/$f" ] || missing=1
    done
    [ "$missing" = 1 ] || return 0
    mkdir -p "$fonts"
    for f in "Arial Narrow=arialn.ttf" "Arial Narrow Bold=arialnb.ttf" "Arial Narrow Italic=arialni.ttf" \
             "Arial Narrow Bold Italic=arialnbi.ttf" "Alpha Centauri=ALPHC___.TTF"; do
        name="${f%%=*}"; f="${f#*=}"
        cp "$ROOT/game/$f" "$fonts/$f"
        wine reg add "$key" /v "$name (TrueType)" /t REG_SZ /d "$f" /f > /dev/null 2>&1
    done
}
wine_install_fonts
