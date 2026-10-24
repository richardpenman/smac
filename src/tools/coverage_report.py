#!/usr/bin/env python3
"""Which reconstructed original functions did native runs execute?

Build with -DSMACX_COVERAGE=ON, run smacx_headless (any number of scenarios: counts accumulate in
the .gcda files), then:

    src/tools/coverage_report.py build/cov-native [--missed] [--module base]

Joins gcov's per-function counts with generated/status.tsv (every ADDR declaration) and prints, per
header, how many implemented original functions ran. --missed lists the ones that never ran: code
paths the native scenarios don't reach, so they are verified only by hybrid/fcompare runs, if at all.
Reset counts by deleting the build's *.gcda files.
"""
import argparse
import csv
import gzip
import json
import os
import subprocess
import sys
from collections import defaultdict


def gcov_functions(build):
    """Map plain function name -> execution count, summed over all objects."""
    gcdas = []
    for root, _, files in os.walk(build):
        gcdas += [os.path.join(root, f) for f in files if f.endswith(".gcda")]
    if not gcdas:
        sys.exit(f"no .gcda files under {build}: build with -DSMACX_COVERAGE=ON and run smacx_headless")
    counts = defaultdict(int)
    for gcda in gcdas:
        out = subprocess.run(["gcov", "--json-format", "--stdout", gcda], cwd=os.path.dirname(gcda),
                             capture_output=True, check=True).stdout
        for line in out.splitlines():  # one JSON document per line
            if not line.strip():
                continue
            if line[:2] == b"\x1f\x8b":
                line = gzip.decompress(line)
            for f in json.loads(line).get("files", []):
                for fn in f.get("functions", []):
                    # demangled "speed(int, int)" or "Path::find(...)" -> "speed"
                    name = fn.get("demangled_name", fn["name"]).split("(")[0].split("::")[-1]
                    counts[name] += fn.get("execution_count", 0)
    return counts


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("build", help="native build directory configured with -DSMACX_COVERAGE=ON")
    ap.add_argument("--missed", action="store_true", help="list implemented functions that never ran")
    ap.add_argument("--module", help="only headers whose name contains this")
    args = ap.parse_args()

    counts = gcov_functions(args.build)
    with open(os.path.join(args.build, "generated", "status.tsv")) as f:
        rows = [r for r in csv.DictReader(f, delimiter="\t") if r["state"] == "implemented"]
    by_header = defaultdict(list)
    for r in rows:
        if args.module and args.module not in r["header"]:
            continue
        by_header[r["header"]].append(r)

    total_run = total = 0
    print(f"{'header':32} {'ran':>5} {'of':>5}")
    for header in sorted(by_header):
        fns = by_header[header]
        ran = [r for r in fns if counts.get(r["name"], 0) > 0]
        total_run += len(ran)
        total += len(fns)
        print(f"{header:32} {len(ran):5} {len(fns):5}")
        if args.missed:
            for r in fns:
                if counts.get(r["name"], 0) == 0:
                    print(f"    never ran: {r['addr']} {r['name']}")
    print(f"{'total':32} {total_run:5} {total:5}")


if __name__ == "__main__":
    main()
