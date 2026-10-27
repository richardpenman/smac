// Replay of golden records (smacx_headless --golden DIR; harness/README.md "Golden records").
//
// The records (tests/golden, src/tools/golden.py) hold the original's results of functions that
// change no game memory, called by fcompare on the states of a harness scenario. smacx_headless
// plays the same scenario and, at the turn_upkeep hook of each recorded turn, checks that the game
// state is the recorded one (state hash) and calls the reconstructed functions with the recorded
// arguments: return value, out-parameters, strings, buffers and random generator states must equal
// the original's.
#pragma once
#include <cstdio>
#include <string>

// Loads SCENARIO's records from DIR. Returns the number of calls (0: none), -1 on a bad file
// (message in *err).
long golden_load(const std::string& dir, const std::string& scenario, std::string* err);
// At the turn_upkeep hook, after the snapshot: replays the records of the current turn.
void golden_hook(FILE* log);
// Writes the summary (log and stdout). Returns 0 when every record of the scenario was replayed
// and matched, 1 otherwise.
int golden_finish(FILE* log);
