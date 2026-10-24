/* Game-state memory regions compared by the differential harness, by original address.
 * Derived from the original savegame serializer (game_data 0x5A6AC0): everything a save stores,
 * minus UI window state and heap pointers. Shared by the oracle, the headless runner and
 * harness/compare.py (which parses this file).
 *
 * REGION(name, addr, size)          fixed region
 * REGION_VAR(name, addr, count_addr, elem_size)   size = *(int32*)count_addr * elem_size
 * Heap arrays use pseudo addresses: MapTiles content at 0x10000000, MapAbstract at 0x20000000.
 * MASK(addr, size)                  bytes ignored by the comparison (pointers)
 */
REGION(GamePrefs, 0x9A6490, 0x398)
REGION(Factions, 0x96C9E0, 0x10660)
REGION(MFactions, 0x946A50, 0x2CE0)
REGION(FactionRankingsUnk, 0x945DD8, 0x20)
REGION(MapHeader, 0x949870, 0xAA4)
MASK(0x94A30C, 8) /* MapTiles, MapAbstract pointers */
REGION_VAR(MapTiles, 0x10000000, 0x949884, 0x2C)
REGION_VAR(MapAbstractData, 0x20000000, 0x94A29C, 1)
REGION(FactionTurnMight, 0x9A68AC, 16000)
REGION(ReplayEventSize, 0x9469F8, 4)
REGION_VAR(ReplayEvents, 0x950324, 0x9469F8, 1)
REGION(Continents, 0x9AA730, 0xE00)
REGION(Units, 0x9AB868, 0x6800)
REGION(UnitsExtra94B558, 0x94B558, 0x1000)
REGION_VAR(Vehs, 0x952828, 0x9A64C8, 0x34)
REGION_VAR(Bases, 0x97D040, 0x9A64CC, 0x134)
REGION(ObjectiveReqVictory, 0x94B4C0, 0x98)
REGION(Monuments, 0x94CA08, 0x27A0)
REGION(TectonicDetonationCount, 0x946138, 0x20)
REGION(DiploStateC, 0x7388D8, 0x2400)
REGION(DiploStateB, 0x93F698, 0x100)
REGION(FactionCombatWin, 0x93A96C, 0x20)
REGION(FactionCombatLoss, 0x93A98C, 0x20)
REGION(CouncilWinState, 0x6FF69C, 0x10)
REGION(CouncilWinVotes, 0x6FF6D4, 0x40)
REGION(CouncilSessionPending, 0x703DE8, 4)
REGION(CouncilProposal, 0x703DEC, 0x20)
REGION(CouncilVoteState, 0x703E0C, 0x20)
REGION(PbemState, 0x93A9AC, 0x4C)
