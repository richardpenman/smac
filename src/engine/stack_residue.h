// Native emulation of values the original leaves on its stack and later code reads (docs/
// RECONSTRUCTION.md, "Known uninitialised reads"). Not original memory.
// Each value emulates an original bug behind a flag of Compat::StackResidue (compat.h).
#pragma once
#include <cstdint>

struct StackResidues {
    // What speed() leaves in the dead stack slot where Path_find later reads its uninitialised
    // "start tile in ZOC" flag (#1): non-zero exactly when the original would leave a non-zero
    // value. Set by speed(), read by Path_move. Compat residue.path_start_zoc.
    int32_t speed = 0;
    // wants_to_attack's uninitialised slot array (#4): [f] is faction f's slot. Cleared by
    // wants_to_attack_fresh at the start of an evaluation, updated by wants_to_attack.
    int32_t wants_to_attack[8] = {};
    // base_minerals' [ebp-4] when it returns (its eco-damage divisor), which base_energy reads for
    // alien factions without trade partners (every caller runs base_minerals right before
    // base_energy). Compat residue.alien_bonus_energy.
    int32_t minerals = 0;
};

inline StackResidues g_stack_residue;
