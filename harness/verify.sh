#!/bin/bash
# Differential verification against cached oracle references.
#
#   harness/verify.sh hybrid [patch-spec] [scenario...]   original + reconstructed functions
#   harness/verify.sh native [scenario...]                pure reconstruction (smacx_headless)
#   harness/verify.sh fcompare [spec] [scenario...]       original vs reconstruction, per function
#
# patch-spec is passed to the hybrid `patch` command (default "all"; e.g. "none +world_build").
# Scenarios are harness/scenarios/*.cmd (default: tiny20 small12 std8). Oracle reference snapshots
# are cached in work/ref/<scenario>/ and created on first use.
# fcompare: runs each scenario with no patches and the `fcompare` command (spec default "all"):
# at every turn the selected functions are called on the live state as original and as
# reconstruction and compared (see harness/README.md). FCOMPARE_OPTS adds options to the command,
# e.g. "hooks=turn,enemy limit=5000,200". Prints per-function mismatches and PASS/FAIL.
# Env: HYBRID_BUILD / NATIVE_BUILD select build directories (default build/hybrid, build/native);
# ORACLE_TIMEOUT (default 1800) limits each oracle/hybrid run in seconds.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MODE="$1"; shift
SPEC="all"
if [ "$MODE" = hybrid ] || [ "$MODE" = fcompare ]; then
  if [ $# -gt 0 ] && [ ! -f "$ROOT/harness/scenarios/$1.cmd" ]; then SPEC="$1"; shift; fi
fi
SCEN="${*:-tiny20 small12 std8}"
FAIL=0
if [ "$MODE" = fcompare ]; then
  for s in $SCEN; do
    OUT="$(mktemp -d "$ROOT/work/verify.XXXXXX")"
    { echo "fcompare 0 999999 ${FCOMPARE_OPTS:-} $SPEC"; cat "$ROOT/harness/scenarios/$s.cmd"; } > "$OUT/fc.cmd"
    PATCH_SPEC="none" ORACLE_DLL=hybrid "$ROOT/harness/run_oracle.sh" "$OUT/fc.cmd" "$OUT/res" 3600 >/dev/null
    SUM="$OUT/res/fcompare_summary.txt"
    echo "[$s]"
    if [ ! -f "$SUM" ]; then
      echo "  FAIL: no fcompare summary"; tail -n 3 "$OUT/res/oracle.log" | sed 's/^/  /'; FAIL=1
    else
      grep -E "^fcompare: (no argument|.* is not reconstructed)" "$OUT/res/oracle.log" | sed 's/^/  /'
      tr -d '\r' < "$SUM" | awk '/^FUNC/ { split($3,c,"="); split($4,m,"="); if (m[2]+0 > 0) bad=bad "  " $2 ": " m[2] " of " c[2] " calls differ\n";
                     if (c[2]+0 == 0) unused=unused " " $2 }
           /^TOTAL/ { tot=$0 }
           END { printf "%s", bad; if (unused != "") print "  not called (empty domains):" unused; print "  " tot }'
      grep -m 10 "^MISMATCH" "$OUT/res/fcompare.log" 2>/dev/null | sed 's/^/    /'
      if tr -d "\r" < "$SUM" | grep -q "^TOTAL.* mismatches=0$"; then echo "  PASS"; else echo "  FAIL"; FAIL=1; fi
      grep -E "CRASH|exit=[1-9]" "$OUT/res/oracle.log" | sed 's/^/  /'
    fi
    rm -rf "$OUT"
  done
  exit $FAIL
fi
for s in $SCEN; do
  CMD="$ROOT/harness/scenarios/$s.cmd"
  REF="$ROOT/work/ref/$s"
  if [ ! -f "$REF/oracle.log" ] || ! grep -q "exit=0" "$REF/oracle.log"; then
    echo "[$s] creating oracle reference..."
    rm -rf "$REF"; "$ROOT/harness/run_oracle.sh" "$CMD" "$REF" "${ORACLE_TIMEOUT:-1800}" >/dev/null
  fi
  OUT="$(mktemp -d "$ROOT/work/verify.XXXXXX")"
  if [ "$MODE" = hybrid ]; then
    PATCH_SPEC="$SPEC" ORACLE_DLL=hybrid "$ROOT/harness/run_oracle.sh" "$CMD" "$OUT" "${ORACLE_TIMEOUT:-1800}" >/dev/null
  else
    BIN="${NATIVE_BUILD:-$ROOT/build/native}/smacx_headless"
    (cd "$ROOT" && "$BIN" "$CMD" --outdir "$OUT" > "$OUT/headless.log" 2>&1)
  fi
  echo "[$s]"
  if ! python3 "$ROOT/harness/compare.py" "$REF" "$OUT" | sed 's/^/  /'; then FAIL=1; fi
  tail -n 3 "$OUT"/oracle.log "$OUT"/headless.log 2>/dev/null | grep -E "CRASH|not reconstructed|exit=[1-9]" | sed 's/^/  /'
  rm -rf "$OUT"
done
exit $FAIL
