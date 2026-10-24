#!/usr/bin/env python3
"""Section-level comparison of two savegames (.sav, version 12 as written by save_daemon).

Usage: savdiff.py A.sav B.sav [--all] [--layout]

Walks the savegame layout of game_data (0x5A6AC0) and reports, per section, how many bytes
differ and the first differing offsets. Encrypted blocks (GameState & 0x10000000) are decrypted
before comparing. Sections that legitimately differ between the original and the reconstruction
are labelled "(expected)":
  legacy-prefs   0x31C bytes of uninitialised stack in the original (zeros in the reconstruction)
  map-header     the MapTiles/MapAbstract heap pointers at +0xA9C/+0xAA0
  mapwin-*       map window view state (UI objects; zero in headless runs)
  canvas         screen size (UI)
--layout prints the section table of A. Exit status 0 if only expected sections differ.
"""
import struct
import sys


def decrypt(data):
    out = bytearray()
    k1 = k2 = 0x80
    for i in range(0, len(data), 2):
        k1 = (k1 + 1) & 0xFF
        k2 = (k2 - 1) & 0xFF
        first, second = data[i + 1], data[i]
        if first and first ^ k1:
            first ^= k1
        if second and second ^ k2:
            second ^= k2
        out += bytes((first, second))
    return bytes(out)


class Reader:
    def __init__(self, data):
        self.data, self.pos, self.sections = data, 0, []

    def take(self, name, size, encrypted=False):
        padded = size + (size & 1) if encrypted else size
        raw = self.data[self.pos:self.pos + padded]
        if len(raw) < padded:
            raise ValueError(f"truncated in {name} at {self.pos:#x}")
        content = decrypt(raw)[:size] if encrypted else raw
        self.sections.append((name, self.pos, padded, content))
        self.pos += padded
        return content

    def i32(self, name, encrypted=False):
        return struct.unpack("<i", self.take(name, 4, encrypted))[0]


def parse(data):
    r = Reader(data)
    r.take("header", 8)
    r.i32("magic")
    legacy = r.take("legacy-prefs", 0x31C)
    if struct.unpack_from("<i", legacy, 0x304)[0] < 11:
        raise ValueError("version < 11 savegame: not supported")
    prefs = r.take("GamePrefs", 0x398)
    game_state, = struct.unpack_from("<I", prefs, 0x30)
    more_rules, = struct.unpack_from("<I", prefs, 0x38C)
    enc = bool(game_state & 0x10000000)
    if enc:
        r.take("GamePrefs(enc)", 0x398, True)
        r.take("DiploPlrList", 0xCE0, True)
        r.take("NetDiploSeq", 4, True)
        r.take("TimeControl", 0xC0, True)
    r.take("CurrentPlayerFaction", 4, enc)
    r.take("Factions", 0x10660, enc)
    r.take("MFactions", 0x2CE0, enc)
    r.take("FactionRankingsUnk", 0x20, enc)
    mh = r.take("map-header", 0xAA4)
    tiles, = struct.unpack_from("<i", mh, 0x14)
    abstract, = struct.unpack_from("<i", mh, 0xA2C)
    r.take("map-tiles", tiles * 0x2C)
    r.take("map-abstract", abstract)
    r.take("FactionTurnMight", 16000, enc)
    n = r.i32("ReplayEventSize", enc)
    if n:
        r.take("ReplayEvents", n, enc)
    r.take("Continents", 0xE00, enc)
    r.take("Units", 0x6800, enc)
    r.take("BungList", 0x1000, enc)
    veh_count, base_count = struct.unpack_from("<ii", prefs, 0x38)
    if veh_count:
        r.take("Vehs", veh_count * 0x34, enc)
    if base_count:
        r.take("Bases", base_count * 0x134, enc)
    if game_state & 0x10:
        r.take("ScenarioFileName", 0x100, enc)
    r.take("GameAltPath", 0x100, enc)
    r.take("Objectives", 0x98, enc)
    r.take("Monuments", 0x27A0, enc)
    r.take("TectonicDetonationCount", 0x20, enc)
    for name in ("InVehMode", "VehID", "CursorX", "CursorY", "Field23D20"):
        r.take("mainview-" + name, 4, enc)
    for w in range(9):  # 8 map windows and the world window, 6 fields each (game_io)
        for _ in range(6):
            r.take(f"mapwin-{w}", 4, enc)
    if more_rules & 0x20:
        for name, size in (("DiploStateC", 0x2400), ("DiploStateB", 0x100), ("FactionCombatWin", 0x20),
                           ("FactionCombatLoss", 0x20)):
            r.take(name, size, enc)
        lists = [f"DiploText[{i}]" for i in range(1, 8)] + \
                [f"DiploMsg[{i}][{j}]" for j in range(1, 8) for i in range(1, j)]
        for name in lists:
            count = r.i32(name + ".count")
            for _ in range(count):
                r.i32(name + ".id")
                length = r.i32(name + ".len")
                if length:
                    r.take(name + ".text", length)
        for name, size in (("CouncilWinState", 16), ("CouncilWinVotesA", 0x20), ("CouncilWinVotesB", 0x20),
                           ("CouncilSessionPending", 4), ("CouncilProposal", 0x20), ("CouncilVoteState", 0x20),
                           ("PbemA", 4), ("PbemB", 4), ("PbemC", 4), ("PbemD", 0x20), ("PbemE", 0x20)):
            if name == "CouncilWinState":
                for k in range(4):
                    r.take(f"CouncilWinState[{k}]", 4, enc)
            else:
                r.take(name, size, enc)
    r.take("canvas", 8)
    r.take("stamp", 4)
    if r.pos != len(data):
        r.sections.append(("trailing", r.pos, len(data) - r.pos, data[r.pos:]))
    return r.sections


EXPECTED = ("legacy-prefs", "mapwin-", "canvas")


def compare(a, b, show_all=False):
    """Compares two savegames section by section. Returns (report lines, ok) where ok is False
    when a section other than the expected ones differs."""
    sa, sb = parse(a), parse(b)
    lines, ok = [], True
    for (na, pa, za, ca), (nb, pb, zb, cb) in zip(sa, sb):
        if na != nb:
            return lines + [f"layout differs: {na} at {pa:#x} vs {nb} at {pb:#x}"], False
        diffs = [i for i in range(min(len(ca), len(cb))) if ca[i] != cb[i]]
        if na == "map-header":
            diffs = [i for i in diffs if not 0xA9C <= i < 0xAA4]
        if len(ca) != len(cb):
            diffs.append(min(len(ca), len(cb)))
        if not diffs:
            continue
        expected = na.startswith(EXPECTED)
        ok = ok and expected
        if expected and not show_all:
            lines.append(f"{na}: {len(diffs)} bytes differ (expected)")
            continue
        first = ", ".join(f"+{i:#x}" for i in diffs[:8])
        lines.append(f"{na} (file {pa:#x}): {len(diffs)} bytes differ{' (expected)' if expected else ''}: {first}")
    if len(sa) != len(sb):
        lines.append(f"section count {len(sa)} vs {len(sb)}")
        ok = False
    return lines, ok


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    a, b = (open(p, "rb").read() for p in args[:2])
    if "--layout" in sys.argv:
        for name, pos, size, _ in parse(a):
            print(f"{pos:#09x} {size:#08x} {name}")
    lines, ok = compare(a, b, "--all" in sys.argv)
    for line in lines:
        print(line)
    print("OK" if ok else "DIFF")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
