#!/bin/bash
# Golden records: the original's results of functions on real game states (tests/golden), so the
# reconstruction can be checked function by function without the original running
# (harness/README.md "Golden records", docs/CLEANUP_PLAN.md increment 17).
#
#   harness/golden.sh record [scenario...]   run the original (hybrid.dll, no patches, Wine) with
#                                            fcompare's golden mode and import the records
#   harness/golden.sh replay [scenario...]   replay them on the reconstruction (smacx_headless
#                                            --golden; what ctest's golden_* tests run)
#
# Scenarios default to tiny20 late140. Recorded: the functions of fcompare_table.h that change no
# game memory (src/tools/golden.py spec), at the turn_upkeep hooks of the turns below, at most
# GOLDEN_LIMIT (default 20) calls per function and hook (small argument domains are enumerated
# completely up to that count). fcompare compares the original with the reconstruction while
# recording; its mismatches are printed (the records stay the original's results either way).
# Env: HYBRID_BUILD / NATIVE_BUILD as for verify.sh (default build/hybrid, build/native); record
# needs hybrid.dll, built from the freeze commit (docs/DEVELOPMENT.md).
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MODE="${1:-}"; shift
SCEN="${*:-tiny20 late140}"
# Recorded turns per scenario: "from to every".
turns() {
  case "$1" in
  tiny20) echo "5 20 7" ;;     # 5, 12, 19
  late140) echo "140 143 2" ;;  # 140, 142
  *) echo "0 999999 10" ;;
  esac
}
FAIL=0
case "$MODE" in
record)
  SPEC="$(python3 "$ROOT/src/tools/golden.py" spec)"
  for s in $SCEN; do
    OUT="$(mktemp -d "$ROOT/work/golden.XXXXXX")"
    { echo "fcompare $(turns "$s" | awk '{ printf "%s %s every=%s", $1, $2, $3 }') limit=${GOLDEN_LIMIT:-20},0 golden $SPEC"
      cat "$ROOT/harness/scenarios/$s.cmd"; } > "$OUT/fc.cmd"
    echo "[$s] recording..."
    PATCH_SPEC="none" ORACLE_DLL=hybrid "$ROOT/harness/run_oracle.sh" "$OUT/fc.cmd" "$OUT/res" 3600 >/dev/null
    if [ ! -s "$OUT/res/golden.tsv" ] || ! grep -q "exit=0" "$OUT/res/oracle.log"; then
      echo "  FAIL: no records"; tail -n 3 "$OUT/res/oracle.log" | sed 's/^/  /'; FAIL=1
    else
      tr -d '\r' < "$OUT/res/fcompare_summary.txt" | awk '/^FUNC/ { split($4,m,"="); if (m[2]+0 > 0) print "  fcompare: " $2 " " $4 }'
      python3 "$ROOT/src/tools/golden.py" import "$s" "$OUT/res/golden.tsv" | sed 's/^/  /' || FAIL=1
    fi
    rm -rf "$OUT"
  done
  ;;
replay)
  BIN="${NATIVE_BUILD:-$ROOT/build/native}/smacx_headless"
  for s in $SCEN; do
    OUT="$(mktemp -d "$ROOT/work/golden.XXXXXX")"
    echo "[$s]"
    (cd "$ROOT" && "$BIN" "harness/scenarios/$s.cmd" --golden tests/golden --outdir "$OUT") | sed 's/^/  /'
    st=${PIPESTATUS[0]}
    grep "golden MISMATCH\|golden: turn .*state" "$OUT/oracle.log" 2>/dev/null | head -n 20 | sed 's/^/    /'
    [ "$st" = 0 ] || [ "$st" = 77 ] || FAIL=1
    rm -rf "$OUT"
  done
  ;;
*)
  sed -n '2,/^set -u/p' "$0" | sed '$d; s/^# \{0,1\}//'; exit 2 ;;
esac
exit $FAIL
