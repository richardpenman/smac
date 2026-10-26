// Compatibility switches (docs/CLEANUP_PLAN.md, section 4). Catalogue: docs/COMPAT.md, generated
// from this file by src/tools/compat.py doc.
//
// Every bug of the original that the reconstruction reproduces (a BUG(original) comment in the
// source) is behind a named flag, and every flag defaults to the original behaviour. A site
// marked COMPAT_ORIGINAL(flag) keeps the original's behaviour; no fixed path is written yet in
// the cleanup phase, so switching its flag off stops the game with a message (compat_unfixed)
// until the fix is written as a deliberate change with its own unit test. A flag can cover
// several sites of the same bug (each site names it); src/tools/compat.py check (part of the
// gate's static step) keeps the sites, the flags and the catalogue in step.
#pragma once
#include <cstdio>
#include <cstdlib>

struct Compat {
    // ---- bugs at several sites ----
    // A vehicle is read after kill() removed it (the next vehicle's data). Fix: read before the kill.
    bool read_after_kill = true;
    // Reads outside a table (Vehs[-1], Bases[-1], Factions[-1], diplo_status[-1] or [8], Units[-1], TechOwners past its
    // end, Facility[negative], ...); the reconstruction reads what the original's memory holds there. Fix: test the
    // index and use the value of "none".
    bool out_of_range_reads = true;
    // arm_val takes an armor id but is passed unit ids (reads past Armor). Fix: pass Units[id].armor_id.
    bool arm_val_unit_ids = true;
    // Queue scans include the slot after the queue (queue_items[queue_size]). Fix: stop at queue_size - 1.
    bool queue_scan_one_past = true;
    // ---- action ----
    // Tests the viewer's entry but names the base of faction_id's entry. Fix: the same entry.
    bool drown_base_entry = true;
    // Compares base_at() with 0 (base 0 counts as no base, no base as a base). Fix: >= 0.
    bool base_at_zero_test = true;
    // ---- ai ----
    // Compares the region of (x, y), not of the neighbour. Fix: the neighbour's.
    bool region_of_origin = true;
    // best_own stays 0 without a target (moves to base 0 if own). Fix: start from -1.
    bool ai_best_own_base_0 = true;
    // Adds faction 0's good tiles of the region. Fix: drop the faction 0 term.
    bool region_faction_0_tiles = true;
    // Loops over all 512 base slots, not BaseCount. Fix: BaseCount.
    bool ai_all_base_slots = true;
    // The prototype loop steps by faction_id. Fix: step by 1 from faction_id * 64.
    bool drop_pod_unit_step = true;
    // ---- ai_move ----
    // best is not reset before the drop search, so it never drops. Fix: reset it.
    bool drop_best_not_reset = true;
    // Tests faction 0's unit count of the region. Fix: the own faction's.
    bool region_faction_0_units = true;
    // Tests the own base for a Psi Gate again instead of the target. Fix: the target base.
    bool psi_gate_own_base = true;
    // The upper bound is tested on this unit's status, not the other's. Fix: the other's.
    bool status_icon_wrong_unit = true;
    // The nine neighbours are all the same tile (ring offset added again). Fix: the neighbour offset.
    bool ring_offset_twice = true;
    // The plan bit is taken from the transport's own region. Fix: the target region.
    bool plan_bit_own_region = true;
    // Checks the own base's sea access instead of the target's. Fix: the target's.
    bool sea_access_own_base = true;
    // Tests the territory owner's status, not the base faction's. Fix: bf.
    bool dstat_territory_owner = true;
    // The improvements are looked up on the unit's own tile. Fix: the target tile.
    bool destroy_own_tile_items = true;
    // ---- base ----
    // Sites are deleted around the last radius tile instead of the base. Fix: around the base.
    bool del_site_last_radius_tile = true;
    // With a full queue the shift reads the word after queue_items. Fix: shift queue_size - 1 items.
    bool queue_shift_past_end = true;
    // The diplomatic weighting uses the best faction found so far (-1 at first). Fix: the faction of base i.
    bool revolt_weight_best_so_far = true;
    // Both passes test the bonus techs of the old faction. Fix: the second pass tests the new faction's.
    bool capture_bonus_old_faction = true;
    // Compares the item with unit 8 instead of the Network Node. Fix: compare with FAC_NETWORK_NODE (-FAC).
    bool psych_request_unit_8 = true;
    // Tests region_total_bases[i] of faction i (the faction as region). Fix: the base's region.
    bool hurry_region_bases_index = true;
    // The improvement search reuses the spreading loop's counter, which ends that loop. Fix: a separate counter.
    bool fungus_spread_loop_counter = true;
    // ---- base_misc ----
    // A per-unit adjustment persists for the following units of base_build's loop. Fix: reset per unit.
    bool base_build_persisting_locals = true;
    // A few facility cases read the population of Bases[BaseCount] (a stale argument slot). Fix: the current base.
    bool base_build_stale_base = true;
    // Adjusts the stored base stack id instead of a score. Fix: adjust the score.
    bool base_build_stack_adjust = true;
    // ---- battle ----
    // The level is -1 without an attacker. Fix: a defined level.
    bool level_without_attacker = true;
    // An artillery duel's single exchange ends early only when displayed. Fix: the same whether displayed or not.
    bool duel_ends_if_displayed = true;
    // ---- colortables ----
    // RGB to HSV compares blue with the maximum instead of the minimum. Fix: compare with the minimum.
    bool hsv_blue_minimum = true;
    // ---- console ----
    // After SEATOOLOW the terraform order is given anyway. Fix: return after the message.
    bool terraform_sea_too_low = true;
    // ---- console_mouse ----
    // A base rally point's leg end stores the tile's y as x and leaves y stale. Fix: store (x, y).
    bool rally_leg_end = true;
    // ---- design ----
    // 256 characters without NUL leave the buffer unterminated. Fix: terminate it.
    bool read_name_unterminated = true;
    // Truncates alt at the (already shortened) length of name. Fix: alt's own length.
    bool alt_truncated_by_name = true;
    // Compares the range with unit_id's chassis instead of u's. Fix: u's.
    bool range_wrong_chassis = true;
    // ---- diplo_act ----
    // Compares the two factions' human bits in place (different bit positions). Fix: compare the bits as booleans.
    bool human_bits_in_place = true;
    // Facility[] is indexed with the negative queue item. Fix: index with -item.
    bool facility_negative_item = true;
    // The bit's byte offset indexes the dword array secret_project_intel. Fix: the dword index.
    bool project_intel_byte_offset = true;
    // Base 0 never receives the headquarters (tests best > 0). Fix: best >= 0.
    bool headquarters_base_0 = true;
    // The old owner's free facilities are also given (with the new owner's techs). Fix: only the new owner's.
    bool capture_old_free_facilities = true;
    // bases_reset gets the faction as the region and the region as the faction. Fix: swap them.
    bool bases_reset_args = true;
    // ---- diplo_comm ----
    // The base named in INOURTERRITORY2 may be -1 (the name before Bases). Fix: skip the name or the message without a
    // base.
    bool territory_base_minus1 = true;
    // The alien variant of INTRODUCEENEMY is chosen by DiploHelpFaction. Fix: by the faction met.
    bool introduce_alien_text = true;
    // DiploRegionFactions[g] is tested as a whole instead of the losing faction's bit. Fix: test 1 << losing.
    bool region_factions_bit = true;
    // ---- diplomacy ----
    // is_coast gets the base whose id is this base's x, and y as the radius flag. Fix: is_coast(base.x, base.y, 0).
    bool is_coast_base_x = true;
    // The armor values are arm_strat of the weapon ids. Fix: of the armor ids.
    bool armor_from_weapon_ids = true;
    // Divides by a faction's ranking, which may be 0. Fix: guard the division.
    bool ranking_divide_zero = true;
    // max instead of min: the whole treasury is given. Fix: min.
    bool gift_whole_treasury = true;
    // The second technology's name is written and then discarded. Fix: keep it in its own slot.
    bool trade_tech2_name_discarded = true;
    // The BASEGIFT label with its suffix is built in an unused buffer. Fix: show the built label.
    bool basegift_label_unused = true;
    // The PEACE partial sums all take 1.5 times the scaled total. Fix: scale each.
    bool peace_partial_sums = true;
    // ---- diplomacy_b ----
    // The friction is read with the receiver's source of the tech in both directions. Fix: the giver's.
    bool tech_friction_receiver = true;
    // base_id keeps its previous value when whose_territory does not write it. Fix: reset it to -1.
    bool territory_base_id_stale = true;
    // The "victim is an AI" test uses the loop counter (8): always true. Fix: the victim.
    bool atrocity_victim_loop_counter = true;
    // "1" is meant for the viewer as victim; the test is victim == f. Fix: victim == viewer.
    bool atrocity_label_victim = true;
    // faction_id is passed to Path_find as the unit id. Fix: a land unit id.
    bool path_unit_faction_id = true;
    // ---- diplowin ----
    // The proposal test reads diplo_status[8] (the loop counter as faction): diplo_agenda[0]. Fix: test the proposing
    // faction's status.
    bool diplo_agenda_loop_counter = true;
    // ---- heap ----
    // Blocks handed out earlier keep pointing into the old buffer if realloc moves it. Fix: a heap that does not move.
    bool heap_realloc_moves = true;
    // ---- infowin ----
    // The line counter is not reset per pane. Fix: reset it per pane.
    bool infowin_counter_per_pane = true;
    // ---- map ----
    // The region number goes to StrBuffer, not to buffer. Fix: append to buffer.
    bool region_number_strbuffer = true;
    // clear_dirty sets every tile's redraw flag instead of clearing it. Fix: clear it.
    bool clear_dirty_sets = true;
    // The previous MapAbstract array is not freed. Fix: free it.
    bool map_resize_abstract_leak = true;
    // ---- move_order ----
    // Tests the moving unit's abilities, not those of the stack member. Fix: the member's.
    bool carried_ability_mover = true;
    // ---- parse ----
    // A marker without digit is found again forever. Fix: copy it and go on.
    bool parse_marker_no_digit = true;
    // ---- path ----
    // The scan reuses the direction loop counter (25): no further neighbour is examined. Fix: a separate counter.
    bool path_scan_dir_counter = true;
    // Tests the tile at index x >> 1 (row 0) for a native base. Fix: map_tile(nx, ny).
    bool native_base_row_0 = true;
    // ---- prefs ----
    // prefs_get_int leaves "0" in StrBuffer, so the default reads "0" + digits. Fix: clear StrBuffer first (done here
    // as the original).
    bool laptop_prefs_strbuffer = true;
    // ---- probe ----
    // Halves the success chance a second time instead of the survival chance. Fix: halve survival.
    bool probe_survival_halved = true;
    // Compares with the status of faction freed + 1. Fix: freed.
    bool probe_freed_status_offset = true;
    // ---- proto ----
    // Tests the last unit of the loop, not the matching one. Fix: the matching one.
    bool prototyped_last_unit = true;
    // ---- rules ----
    // The matching social effect index is not stored (-1). Fix: store i.
    bool social_effect_not_stored = true;
    // ---- save ----
    // The pair index is compared with the byte count (an odd size reads one byte past the end). Fix: compare with the
    // pair count.
    bool encrypt_odd_size = true;
    // Old faction conversion keeps 5 of 8 facility bytes and stale high bytes. Fix: copy all 8 and clear the rest.
    bool faction_convert_facility_bytes = true;
    // Old base conversion keeps 5 of 8 facility bytes, stale high bytes and pad_5. Fix: copy all and clear the rest.
    bool base_convert_facility_bytes = true;
    // fread of an empty text returns 0 and fails the load. Fix: skip the read for len 0.
    bool empty_text_fails_load = true;
    // The legacy preferences block is written from an uninitialised stack buffer (written as zeros, version 12, here).
    // Fix: as here.
    bool legacy_prefs_uninit = true;
    // For savegames map_data reads the blocks with an old layout (wrong offset). Fix: the current layout.
    bool map_data_save_layout = true;
    // fclose is also called when the file could not be created. Fix: only when open.
    bool fclose_null_file = true;
    // Undo files are named without extension while save_daemon appends ".SAV". Fix: the same name.
    bool undo_name_extension = true;
    // Renames the previous (already moved) slot onto this one: the slots do not rotate. Fix: rotate from the top.
    bool undo_rotation = true;
    // ---- score ----
    // Tests +102 (a unit) in the queue instead of -102 (the Ascent). Fix: -FAC_ASCENT_TO_TRANSCENDENCE.
    bool ascent_queue_sign = true;
    // Unit counts of 251 and more are ignored (the byte counter can wrap). Fix: a wider counter.
    bool unit_count_251 = true;
    // Integer quotient first, so weaker weapons count 0. Fix: multiply first.
    bool weapon_ratio_integer = true;
    // Divides by zero (crash) when no population counts; the reconstruction yields 0. Fix: as here.
    bool score_divide_zero = true;
    // No bounds check: past 1024 events the record is written beyond ReplayEvents. Fix: stop recording.
    bool replay_events_overflow = true;
    // ---- setup ----
    // The distance to other units is measured from (x, y), not from the candidate. Fix: from the candidate.
    bool setup_site_distance_origin = true;
    // With no free prototype slot the sea pod prototype id is killer_id. Fix: skip the prototype.
    bool sea_pod_slot_killer_id = true;
    // set_fac arguments out of order: facility 4 set at base 1. Fix: set_fac(FAC_PERIMETER_DEFENSE, base, 1).
    bool perimeter_defense_args = true;
    // The project draw never ends when projects 0-7 are all built. Fix: give up after the eight.
    bool time_warp_project_loop = true;
    // Compares the tile's region with itself (always skipped). Fix: compare with the base's region.
    bool setup_region_self_compare = true;
    // The morale goes to the vehicle created last before, not the new one. Fix: the new vehicle.
    bool setup_morale_last_veh = true;
    // The sea escape pod uses a stale location. Fix: the base's location.
    bool setup_escape_pod_location = true;
    // ---- stack ----
    // Tests the base nearest to the unit, not to the sensor tile. Fix: the sensor tile.
    bool sensor_base_nearest_unit = true;
    // Tests the visibility of the spotting unit, not the spotted one. Fix: the spotted one.
    bool spot_visibility_spotter = true;
    // Tests the position of veh_id instead of vehicle i. Fix: vehicle i.
    bool promote_position_veh_id = true;
    // No map bounds check before map_tile. Fix: check map_in_bounds.
    bool stack_map_bounds = true;
    // ---- strings ----
    // purge_leading discards its scan: the string is not modified. Fix: move the text after the spaces.
    bool purge_leading_noop = true;
    // ---- tech ----
    // has_tech rejects tech 88 (the last one). Fix: accept ids up to 88.
    bool last_tech_never_counts = true;
    // ---- text ----
    // At end of file a null string is stored (Strings_put(nullptr) reads address 0). Fix: store the empty buffer.
    bool text_null_string = true;
    // ---- turn ----
    // Kelp farm / tidal platform destruction requires an ocean tile that is also land: never happens. Fix: test ocean
    // only.
    bool sea_wipe_never = true;
    // The shown base is 1 (base 0) when only the event base's visibility counts. Fix: the event base.
    bool event_shown_base_0 = true;
    // Clears the viewer's tile memory seven times instead of every faction's. Fix: every faction's.
    bool landmark_memory_viewer = true;
    // The spreading tile's visibility decides who learns of the new one; the worm spawn redraws a stale tile. Fix: the
    // new tile.
    bool spread_visibility_source = true;
    // The worms-attack-base code after the loop is unreachable. Fix: exit the loop when a base is found.
    bool fungal_worms_unreachable = true;
    // turn_upkeep returns early while an aircraft in flight has moves, skipping the rest of the upkeep. Fix: continue
    // with the next unit.
    bool turn_upkeep_air_return = true;
    // Tests the current base, not base i. Fix: base i.
    bool drone_riot_current_base = true;
    // Mixes ranks and faction ids when choosing the rival. Fix: map ranks to factions.
    bool council_rank_faction_mix = true;
    // Restarts the round at faction 1 after a human move. Fix: continue with the next faction.
    bool net_round_restart = true;
    // ---- veh ----
    // Compares the faction's own base count in the region, not the enemy's. Fix: the enemy's.
    bool enemy_region_own_count = true;
    // ---- vehdraw ----
    // The flag markers' position goes to the slot of a stale loop counter. Fix: store it in slot 0, which veh_draw
    // reads.
    bool flag_marker_stale_slot = true;
    // The faction's free abilities the unit already has are drawn twice. Fix: skip abilities the unit has.
    bool free_abilities_twice = true;
    // ---- voxel_render ----
    // The y axis length uses matrix elements 4 and 5. Fix: elements 4 and 6 (or 3 and 5, as the x axis).
    bool voxel_y_axis_elements = true;
    // ---- world ----
    // For altitude 6 the upper bound is entry 10, not 7. Fix: entry alt + 1.
    bool alt_upper_bound_6 = true;
    // Emulated stack residues and uninitialised reads of the original (docs/RECONSTRUCTION.md,
    // "Known uninitialised reads"). The native values live in g_stack_residue (stack_residue.h),
    // enemy_move's slots in its EM frame (ai_move.cpp), order_veh's in OrderVeh (move_order.cpp).
    struct StackResidue {
        // The alien energy bonus starts from a local only the commerce loop writes (residue of base_minerals
        // otherwise). Fix: start from 0.
        bool alien_bonus_energy = true;
        // A negative position reads values[] of the sort frame. Fix: reject negative positions.
        bool base_list_frame = true;
        // Revolt shares of factions without the bonus are never written (stack residue, 0 under the oracle). Fix:
        // initialise them to 0 (as now).
        bool revolt_shares = true;
        // Path_find's start-ZOC flag is an uninitialised local (the residue speed() leaves, g_stack_residue.speed). Fix: 0.
        bool path_start_zoc = true;
        // call_council's "aliens alive" flag is the caller's ECX unless proposal 1 is considered. Fix: compute it for
        // every proposal.
        bool council_aliens_ecx = true;
        // score is read before it is written (the 0x3039 test). Fix: initialise it.
        bool ai_score = true;
        // gender is uninitialised when "$<#n" sets only the plurality. Fix: 0.
        bool parse_gender = true;
        // The per-faction maxima of region_total_bases start from uninitialised stack slots (uninitialised read #4).
        // Fix: start from 0.
        bool region_maxima = true;
        // enemy_move reads [ebp-0x38], not written on this path in the call. Fix: 0.
        bool enemy_move_s38 = true;
        // [ebp-0x48] is read where it was not written (uninitialised read #2). Fix: 0.
        bool enemy_move_s48 = true;
        // EDI holds [ebp-0x38], unwritten when no transport was examined (uninitialised read #3). Fix: 0.
        bool enemy_move_edi = true;
        // order_veh's post-move code reads [ebp-0x24] ("move shown to the viewer"), which a won battle leaves
        // holding the target's stack offset and the dir 8 path leaves unset (0 here; uninitialised read #5). Fix:
        // compute whether the unit is shown on these paths too.
        bool order_veh_visible = true;
    } residue;
};

inline Compat g_compat;

// A COMPAT_ORIGINAL site was reached with its flag switched off: no fixed path exists yet.
[[noreturn]] inline void compat_unfixed(const char* flag) {
    std::fprintf(stderr, "compat: the fix for %s is not written yet (src/engine/compat.h)\n", flag);
    std::abort();
}

// Marks a site that keeps a bug of the original (see the BUG(original) comment next to it).
#define COMPAT_ORIGINAL(flag) (g_compat.flag ? (void)0 : compat_unfixed(#flag))
