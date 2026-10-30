#!/usr/bin/env python3
"""Compare the sound calls of the original (oracle "logsound": SOUND T<turn> <name> <args> from
<caller>) with OpenSMAC's (OPENSMAC_SOUNDLOG=1: sound: T<turn> <name> <args>).

  soundcmp.py ORACLE_LOG|REF OPENSMAC_LOG [--names a,b,..] [--one-window]
  soundcmp.py --normalize ORACLE_LOG > REF      (the reference kept in tests/sound)

--one-window: the original repeats an explosion's sound for every map window showing its tile
(boom's loop over MapWins); OpenSMAC has one map window: an immediately repeated explosion sound
(FX_play_loc 61, 62 or 90 and its Wave_play_2) of the original counts once.

Both are reduced to lines "T<turn> <name> <args>" (Wave_play_2: the file's base name, lower case;
FX_play_loc: id only, as the location is not used by the original's FX_play_loc) in call order, for
the functions both log; prints the first differences and exits 1 when the sequences differ."""
import re
import sys

NAMES = ["FX_play", "FX_play_loc", "FX_fade", "FX_stop", "wave_it", "ambience", "ambience_veh", "boom",
         "movie", "load_music", "popup_wave", "Wave_play_2"]


def norm(name, args):
    if name == "Wave_play_2":
        f = args[-1] if args else "-"
        return "Wave_play_2 " + re.split(r"[\\/]", f)[-1].lower()
    if name == "FX_play_loc":
        return "FX_play_loc " + args[0]
    if name == "load_music":
        return "load_music " + args[0]
    return " ".join([name] + args)


def read(path, oracle, names):
    out = []
    for line in open(path, errors="replace"):
        if oracle and re.match(r"^T-?\d+ \S", line):  # a normalized reference
            if line.split()[1] in names:
                out.append(line.strip())
            continue
        if oracle:
            m = re.match(r"SOUND T(-?\d+) (\S+) ?(.*?) from [0-9A-F]+$", line.strip())
            if not m:
                continue
            turn, name, rest = m.groups()
            args = rest.split()
            if name == "Wave_play_2":
                args = [rest.split(" ", 1)[1] if " " in rest else "-"]
        else:
            m = re.match(r"sound: T(-?\d+) (\S+) ?(.*)$", line.strip())
            if not m:
                continue
            turn, name, rest = m.groups()
            args = [rest] if name == "Wave_play_2" else rest.replace("(", " ").replace(")", " ").replace(",", " ").split()
        if name not in names:
            continue
        out.append("T%s %s" % (turn, norm(name, args)))
    return out


def one_window(seq):
    out = []
    for x in seq:
        t, rest = x.split(" ", 1)
        if (len(out) >= 2 and rest.split()[0] == "FX_play_loc" and rest.split()[1] in ("61", "62", "90") and
                out[-2] == x):
            out.append(None)  # drop this repeat and its Wave_play_2
            continue
        if out and out[-1] is None:
            out.pop()
            if rest.startswith("Wave_play_2"):
                continue
        out.append(x)
    return [x for x in out if x is not None]


def main():
    names = NAMES
    if "--normalize" in sys.argv:
        for x in read(sys.argv[sys.argv.index("--normalize") + 1], True, names):
            print(x)
        return 0
    if "--names" in sys.argv:
        names = sys.argv[sys.argv.index("--names") + 1].split(",")
    a = read(sys.argv[1], True, names)
    b = read(sys.argv[2], False, names)
    if "--one-window" in sys.argv:
        a = one_window(a)
    print("original: %d calls, OpenSMAC: %d calls" % (len(a), len(b)))
    if a == b:
        print("OK: identical sequences")
        return 0
    # Per turn: the turns whose sequences differ, with both sequences' differences.
    import difflib
    from collections import OrderedDict
    def by_turn(seq):
        d = OrderedDict()
        for x in seq:
            t, rest = x.split(" ", 1)
            d.setdefault(t, []).append(rest)
        return d
    ta, tb = by_turn(a), by_turn(b)
    bad = 0
    for t in sorted(set(ta) | set(tb), key=lambda k: int(k[1:])):
        sa, sb = ta.get(t, []), tb.get(t, [])
        if sa == sb:
            continue
        bad += 1
        print("%s differs (original %d, OpenSMAC %d):" % (t, len(sa), len(sb)))
        for d in list(difflib.unified_diff(sa, sb, lineterm="", n=0))[2:14]:
            print("   " + d)
    print("%d turns differ" % bad)
    return 1


if __name__ == "__main__":
    sys.exit(main())
