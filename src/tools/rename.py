#!/usr/bin/env python3
"""Rename an identifier across the tree (cleanup phase, docs/CLEANUP_PLAN.md "Renames").

  rename.py OLD NEW [--dry-run]

Replaces the whole word OLD by NEW in every git-tracked (or new, not ignored) text file under
src/, harness/, tests/, docs/ and README.md (code, the fcompare table, scenario patch specs, prose), then refreshes
re/original_names.tsv (the original name column is kept, so the address stays findable under its
old name). Refuses when NEW already occurs as a word anywhere in those files (a collision, or an
overload set the build would not catch) or when OLD does not occur.

A rename is proven by the build (every use must still compile and link: gen_bindings.py and the
hybrid patch table use the new name) and by src/tools/gate.sh. Word-level replacement is right for
this code base's identifiers (C-style free functions and globals with unique names); a member or
local name shared by several classes needs clangd's rename instead (compile_commands.json is in
every build directory).
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SCOPES = ["src", "harness", "tests", "docs", "README.md"]
TEXT_EXT = {".h", ".c", ".cpp", ".py", ".sh", ".md", ".cmd", ".tsv", ".txt", ".steps", ""}


def tracked_files():
    cmd = ["git", "ls-files", "--cached", "--others", "--exclude-standard", "--"] + SCOPES
    out = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, check=True).stdout.split("\n")
    files = []
    for f in out:
        if f and os.path.splitext(f)[1] in TEXT_EXT and os.path.isfile(os.path.join(ROOT, f)):
            files.append(f)
    return files


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    dry = "--dry-run" in sys.argv
    if len(args) != 2 or not all(re.fullmatch(r"[A-Za-z_]\w*", a) for a in args):
        print(__doc__)
        return 2
    old, new = args
    old_re, new_re = re.compile(rf"\b{old}\b"), re.compile(rf"\b{new}\b")
    changes = []
    for f in tracked_files():
        if f == "re/original_names.tsv":
            continue
        path = os.path.join(ROOT, f)
        try:
            text = open(path, encoding="utf-8", newline="").read()
        except UnicodeDecodeError:
            continue
        if new_re.search(text):
            print(f"rename.py: {new} already occurs in {f}; choose another name")
            return 1
        n = len(old_re.findall(text))
        if n:
            changes.append((f, n, old_re.sub(new, text)))
    if not changes:
        print(f"rename.py: {old} not found")
        return 1
    for f, n, text in changes:
        print(f"  {f}: {n}")
        if not dry:
            with open(os.path.join(ROOT, f), "w", encoding="utf-8", newline="") as fh:
                fh.write(text)
    if not dry:
        subprocess.run([sys.executable, os.path.join(ROOT, "src", "tools", "names.py"), "update"], check=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
