#!/bin/bash
# Run the original terranx.exe under Wine with oracle.dll injected.
# Usage: harness/run_oracle.sh <script.cmd> <outdir> [timeout_seconds]
# Env: ORACLE_DLL=oracle|hybrid (default oracle) selects the injected DLL;
#      HYBRID_BUILD=<dir> (default build/hybrid) is where the DLLs are taken from.
#      PATCH_SPEC="<spec>" (hybrid only) is applied at DLL load, before the game boots.
#      ORACLE_SCREEN=WxH (default 1024x768) is the Xvfb screen: the original runs at its size.
# Creates an isolated run directory (symlinked game data + pristine ini), runs the script, and
# copies results into <outdir>.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SCRIPT="$(realpath "$1")"
OUT="$(realpath -m "$2")"
TIMEOUT="${3:-600}"
RUN="$(mktemp -d "$ROOT/work/run.XXXXXX")"
for f in "$ROOT"/game/*; do ln -s "$f" "$RUN/"; done
cp "$ROOT/harness/oracle/runtime/Alpha Centauri.Ini" "$RUN/"
DLL="${ORACLE_DLL:-oracle}.dll"
BDIR="${HYBRID_BUILD:-$ROOT/build/hybrid}"
cp "$BDIR/$DLL" "$BDIR/oracle_launch.exe" "$RUN/"
# saves dir must be writable and private
rm -f "$RUN/saves" && mkdir -p "$RUN/saves"
sed 's/^outdir .*/outdir oracle_out/' "$SCRIPT" > "$RUN/oracle.cmd"
[ -n "${PATCH_SPEC:-}" ] && echo "$PATCH_SPEC" > "$RUN/oracle.patch"
grep -q '^outdir' "$RUN/oracle.cmd" || sed -i '1i outdir oracle_out' "$RUN/oracle.cmd"
# The prefix, the game's fonts and Windows' text measurement (harness/wine_env.sh).
. "$ROOT/harness/wine_env.sh"
# private X display per run
DISP=$(( 100 + RANDOM % 800 ))
Xvfb ":$DISP" -screen 0 "${ORACLE_SCREEN:-1024x768}x24" >/dev/null 2>&1 &
XPID=$!
sleep 1
cd "$RUN"
DISPLAY=":$DISP" timeout "$TIMEOUT" wine oracle_launch.exe "$DLL" > "$RUN/wine.log" 2>&1 &
WPID=$!
# screenshot shortly before the timeout so hangs can be diagnosed
( sleep $(( TIMEOUT > 20 ? TIMEOUT - 10 : TIMEOUT / 2 )); DISPLAY=":$DISP" "$ROOT/tools/venv/bin/python" "$ROOT/tools/shot.py" "$RUN/oracle_out/hang.png" 2>/dev/null ) &
SPID=$!
wait $WPID
RC=$?
pkill -P $SPID 2>/dev/null; kill $SPID 2>/dev/null
[ $RC -ne 124 ] && rm -f "$RUN/oracle_out/hang.png"
# wait for this run's wine processes to exit (do not kill other runs sharing the prefix)
sleep 1
kill $XPID 2>/dev/null
mkdir -p "$OUT"
cp -r "$RUN/oracle_out/." "$OUT/" 2>/dev/null
grep -v "^ALSA" "$RUN/wine.log" > "$OUT/wine.log" 2>/dev/null
echo "exit=$RC" >> "$OUT/oracle.log"
rm -rf "$RUN"
exit $RC
