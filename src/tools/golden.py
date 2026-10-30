#!/usr/bin/env python3
"""Golden records: the original's results of functions on real game states, kept in tests/golden/
so the reconstruction can be checked function by function without the original running
(docs/CLEANUP_PLAN.md, section 8 and increment 17; harness/README.md "Golden records").

  golden.py import SCENARIO GOLDEN.TSV [DIR]   import a recording (fcompare ... golden, written by
                     hybrid.dll as golden.tsv) as SCENARIO's records: DIR/states.tsv (state hash per
                     turn) and DIR/<function>.tsv (one row per call). Replaces SCENARIO's earlier
                     rows; DIR defaults to tests/golden.
  golden.py gen OUT.h  write the replay table of opensmac_headless --golden (the build runs this): the
                     functions of harness/oracle/fcompare_table.h that change no game memory (no
                     FC_MUTATES, no FC_PATHS), with the headers that declare them
                     (re/original_names.tsv). NOT_RECORDED below lists the exceptions.
  golden.py spec     print the fcompare spec that selects those functions ("none +a +b ...").
  golden.py list [DIR] print the scenarios, turns and calls per function of the records.

Record rows (tab-separated): scenario, turn, arguments, return, outputs, crt state, game state.
Arguments are comma-separated: a number, '&' (int32 out-parameter), '&s' (string buffer), 'bN'
(int32 buffer from seed N, fcompare's FA_BUF) or 'null'. Outputs, in argument order: the
out-parameter's value, 's' + the string (%XX escapes), 'h' + the FNV-1a hash of the buffer.
'-' stands for none (no arguments, a void function, no outputs).
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TABLE = os.path.join(ROOT, "harness", "oracle", "fcompare_table.h")
NAMES = os.path.join(ROOT, "re", "original_names.tsv")
DEFAULT_DIR = os.path.join(ROOT, "tests", "golden")
# Functions of the table that are not recorded, and why.
NOT_RECORDED = {
    "mandate_color": "returns UI palette indices (MandateColors) that the UI sets up and opensmac_headless does not",
}
HEADER = "scenario\tturn\targs\treturn\toutputs\tcrt\tgame"
STATES_HEADER = "scenario\tturn\tstate_hash"


def eligible_functions():
    """Names of the fcompare table entries that change no game memory, in table order."""
    text = re.sub(r"/\*.*?\*/", "", open(TABLE, encoding="utf-8").read(), flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    names = []
    for m in re.finditer(r'^\s*\{\s*"(\w+)"\s*,\s*([^,{]*),', text, re.M):
        flags = m.group(2)
        if "FC_MUTATES" in flags or "FC_PATHS" in flags or m.group(1) in NOT_RECORDED:
            continue
        if m.group(1) not in names:
            names.append(m.group(1))
    return names


def read_rows(path):
    if not os.path.exists(path):
        return []
    return [line.split("\t") for line in open(path, encoding="utf-8").read().splitlines()[1:] if line]


def write_rows(path, header, rows):
    if not rows:
        if os.path.exists(path):
            os.remove(path)
        return
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(header + "\n")
        for r in rows:
            f.write("\t".join(r) + "\n")


def cmd_import(scenario, tsv, out):
    os.makedirs(out, exist_ok=True)
    states, calls = [], {}
    seen, skip = set(), False
    for line in open(tsv, encoding="utf-8", errors="replace").read().splitlines():
        p = line.rstrip("\r").split("\t")
        if p[0] == "STATE":
            # turn_upkeep can run twice for one turn (an aircraft in flight); the replay checks
            # the first hook of each turn only
            skip = p[1] in seen
            if skip:
                print(f"golden.py: turn {p[1]} recorded twice; the second hook is dropped")
                continue
            seen.add(p[1])
            states.append([scenario, p[1], p[2]])
        elif len(p) == 7 and not skip:
            calls.setdefault(p[0], []).append([scenario] + p[1:])
        elif not skip:
            sys.exit(f"golden.py: bad line in {tsv}: {line[:80]}")
    if not states:
        sys.exit(f"golden.py: {tsv} holds no records")
    turn_key = lambda r: int(r[1])
    old = [r for r in read_rows(os.path.join(out, "states.tsv")) if r[0] != scenario]
    write_rows(os.path.join(out, "states.tsv"), STATES_HEADER,
               sorted(old + states, key=lambda r: (r[0], turn_key(r))))
    known = set(eligible_functions())
    for f in sorted(set(calls) - known):
        sys.exit(f"golden.py: {f} is recorded but changes game memory or has no table entry")
    existing = [n[:-4] for n in os.listdir(out) if n.endswith(".tsv") and n != "states.tsv"]
    total = 0
    for name in sorted(set(existing) | set(calls)):
        path = os.path.join(out, name + ".tsv")
        rows = [r for r in read_rows(path) if r[0] != scenario] + calls.get(name, [])
        rows.sort(key=lambda r: (r[0], turn_key(r)))  # stable: the recorded order within a turn
        write_rows(path, HEADER, rows)
        total += len(calls.get(name, []))
    print(f"golden.py: {scenario}: {len(states)} states, {len(calls)} functions, {total} calls -> "
          f"{os.path.relpath(out, ROOT)}")
    return 0


def cmd_gen(out):
    headers = {}
    for line in open(NAMES, encoding="utf-8").read().splitlines()[1:]:
        p = line.split("\t")
        headers[p[2]] = p[4]
    names = eligible_functions()
    missing = [n for n in names if n not in headers]
    if missing:
        sys.exit("golden.py: not declared with ADDR (re/original_names.tsv): " + " ".join(missing))
    lines = ["// Generated by src/tools/golden.py gen from harness/oracle/fcompare_table.h; do not edit.",
             "// The functions whose golden records opensmac_headless --golden replays.",
             "#ifdef GOLDEN_INCLUDES"]
    lines += [f'#include "{h}"' for h in sorted({headers[n] for n in names})]
    lines += ["#else"] + [f"GOLDEN_FUNC({n})" for n in names] + ["#endif", ""]
    text = "\n".join(lines)
    if not os.path.exists(out) or open(out, encoding="utf-8").read() != text:
        open(out, "w", encoding="utf-8", newline="\n").write(text)
    return 0


def cmd_list(out):
    for r in read_rows(os.path.join(out, "states.tsv")):
        print(f"state {r[0]} turn {r[1]} {r[2]}")
    for n in sorted(os.listdir(out)):
        if n.endswith(".tsv") and n != "states.tsv":
            rows = read_rows(os.path.join(out, n))
            per = {}
            for r in rows:
                per[r[0]] = per.get(r[0], 0) + 1
            print(f"{n[:-4]}: " + ", ".join(f"{s} {c}" for s, c in sorted(per.items())))
    return 0


def main():
    a = sys.argv[1:]
    if len(a) >= 3 and a[0] == "import":
        return cmd_import(a[1], a[2], os.path.abspath(a[3]) if len(a) > 3 else DEFAULT_DIR)
    if len(a) == 2 and a[0] == "gen":
        return cmd_gen(a[1])
    if a == ["spec"]:
        print("none " + " ".join("+" + n for n in eligible_functions()))
        return 0
    if a and a[0] == "list":
        return cmd_list(os.path.abspath(a[1]) if len(a) > 1 else DEFAULT_DIR)
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())
