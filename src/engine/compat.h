// The original's bugs (docs/CLEANUP_PLAN.md, section 4). Catalogue: docs/COMPAT.md, generated
// from this file by src/tools/compat.py doc.
//
// Every bug of the original that the reconstruction knows of (a BUG(original) comment in the
// source) is behind a named flag of struct Compat: true plays the original's behaviour, false the
// fix. The player chooses only between two settings (compat_set_original_bugs): the fixes (the
// default: every flag at its default value below, false for a fixed bug) or the original bugs
// (smacx --original-bugs, or "Original Bugs=1" in the ini: every flag true, the game exactly as
// terranx.exe plays it). Everything compared with the original (the harness, the frozen baseline,
// the scripted suites whose references come from the original) runs with the original bugs.
// The single flags exist for the code and the tests (a test can switch one fix).
//
// Each flag's comment says what the bug does in play, then "Fix:" what the fixed path does. A
// flag whose fix is deliberately not written defaults to true and says why after "Not fixed:";
// its sites keep the original's code with COMPAT_ORIGINAL(flag), which stops the game
// (compat_unfixed) if a test switches it off. A flag can cover several sites of the same bug
// (each site names it); src/tools/compat.py check (the gate's static step) keeps the sites, the
// flags and the catalogue in step.
#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>

struct Compat {
    // ---- bugs at several sites ----
    // After a missile or a unit is removed (kill), the code reads the removed unit again, which is now the next unit
    // in the list: a tectonic missile's message names, and its detonation is counted for, another unit's faction; a
    // fungal missile's spread uses another unit's reactor and faction; combat reads the wrong unit after a loss. Fix:
    // the values are read before the unit is removed.
    bool read_after_kill = false;
    // In many places the original reads outside a table when an index is "none" (-1) or one too large: Vehs[-1] when
    // there is no unit (combat with no attacker or no defender, units next to a tile, the killer of a unit),
    // Bases[-1] when there is no base (sensor and capture tests, messages, the AI's former and unit choices),
    // diplo_status[-1] or [8] (a faction "none" or a zone-of-control result), technology ids past the table in
    // trades, a council vote's candidate. The reconstruction reads what the original's memory holds there, so the
    // results are arbitrary: e.g. a probe team's defence, an AI's attack odds against an empty tile, a phantom energy
    // reward. Fix: each site tests the index and uses the value of "none": no unit, no faction, no base, no status,
    // no owner, the candidate's own values; a battle without a defender is no battle.
    bool out_of_range_reads = false;
    // Where the AI weighs armour (its choice of what to build, and the disbanding of obsolete units), arm_val(),
    // which takes an armour id, is given unit ids: for most units it reads past the armour table into unrelated data,
    // so the AI compares random-looking armour values. Fix: the unit's armour is looked up (armor_val:
    // Units[id].armor_id).
    bool arm_val_unit_ids = false;
    // Loops over a base's production queue include queue_items[queue_size], believed to be one past the queue. Not
    // fixed: not a bug: queue_size is the index of the queue's last item (the queue holds queue_size + 1 items, as
    // the base window shows and fills them), so these loops cover exactly the queue; stopping at queue_size - 1 would
    // skip the last item.
    bool queue_scan_one_past = true;
    // ---- action ----
    // After a terraforming action floods a tile (sea level, a raised or lowered tile), the "units drowned near base"
    // message tests the viewer's entry of the drowned-units table but names the base from the acting faction's entry:
    // the message can be missing, or name a wrong or no base. Fix: the acting faction's entry for both.
    bool drown_base_entry = false;
    // When an aircraft's patrol is checked for its range, "is there a base" compares base_at() with 0 instead of
    // testing for -1: base 0 counts as no base and an empty tile counts as a base, so the range allowed is wrong.
    // Fix: a base is base_at() >= 0.
    bool base_at_zero_test = false;
    // ---- ai ----
    // coast_or_border() looks for a neighbouring tile on the coast, outside the faction's territory or on another
    // continent, but compares the continent of the centre tile instead of the neighbour's, so the "other continent"
    // test never finds anything. Fix: the neighbour's continent.
    bool region_of_origin = false;
    // An AI unit that found no better target (a Planet Buster, or a unit choosing a base to return to) is sent to
    // base 0 if base 0 belongs to its faction, because the best own base starts as 0. Fix: it starts as none (-1):
    // the unit stays.
    bool ai_best_own_base_0 = false;
    // When the AI decides whether to keep settling a continent, it adds the native faction's (faction 0) count of
    // good tiles there to its own, so it overestimates the room left. Fix: only its own count.
    bool region_faction_0_tiles = false;
    // When an AI missile looks for a base of its own to fly to, it loops over all 512 base slots instead of the bases
    // in play: the slots past the last base hold stale copies of destroyed or captured bases, so missiles can be sent
    // towards bases that no longer exist (or no longer belong to the faction). Fix: only the bases in play
    // (BaseCount).
    bool ai_all_base_slots = false;
    // When the AI rates a conventional missile strike, it counts its drop-pod units by stepping through the prototype
    // table by the faction's id from slot 64 instead of through its own 64 prototype slots, so it counts other
    // factions' designs and misses its own. Fix: the faction's own prototype slots, one by one.
    bool drop_pod_unit_step = false;
    // ---- ai_move ----
    // An AI drop-pod unit with no target in range should drop next to an enemy base, but the best score is not reset
    // after the previous search (it is still 99999), so no site ever scores higher and it never drops. Fix: the best
    // score starts from 0: the unit drops next to the best enemy base.
    bool drop_best_not_reset = false;
    // Whether an AI defender may leave its base depends on the combat units in the region, but it reads the native
    // faction's (faction 0) count instead of its own faction's. Fix: the own faction's count.
    bool region_faction_0_units = false;
    // When an AI unit considers using a Psi Gate, the destination base is to have a Psi Gate too, but the test checks
    // the unit's own base again, so units teleport to bases without a gate. Fix: the destination base must have a Psi
    // Gate.
    bool psi_gate_own_base = false;
    // When AI attackers gather near a base before an assault, the count of others already waiting tests the range of
    // the other unit's status with this unit's status for the upper bound, so the count is wrong. Fix: the other
    // unit's status for both bounds.
    bool status_icon_wrong_unit = false;
    // When an AI explorer rates unexplored tiles, the nine tiles around a candidate are all the same tile (the ring
    // offset is added twice instead of the neighbour offset), so explorers judge tiles by one far-off spot. Fix: the
    // candidate's own tile and its eight neighbours.
    bool ring_offset_twice = false;
    // An AI transport looking for the coast of an empty continent checks whether the transport's own continent's plan
    // fits its passengers, instead of the target continent's. Fix: the target continent's plan.
    bool plan_bit_own_region = false;
    // Raiding AI factions (sea-power factions) pick enemy ports to raid, but at sea the reachability test checks the
    // own base's access to the sea region instead of the target's. Fix: the target base's.
    bool sea_access_own_base = false;
    // When an AI unit considers moving into an empty enemy base, it checks its diplomatic status with the owner of
    // the territory the base stands on instead of the base's owner. Fix: the base owner's status.
    bool dstat_territory_owner = false;
    // AI artillery looking for enemy improvements to destroy checks the improvements of its own tile instead of the
    // target tile, so it shells tiles without improvements and ignores those with. Fix: the target tile's
    // improvements.
    bool destroy_own_tile_items = false;
    // ---- base ----
    // When a base is founded, the AI's planned base sites near it should be deleted around the new base, but they are
    // deleted around the last tile of its radius loop (3 tiles away), so AI colony pods keep heading for sites next
    // to the new base and others are dropped. Fix: the sites around the base itself.
    bool del_site_last_radius_tile = false;
    // When a base's production moves to the next queued item with a full queue (queue_size 10, which the base window
    // can reach by inserting into a full queue), the shift reads the word after the queue (the base's worked tiles)
    // as a queue item. Fix: the shift stays within the queue's ten entries.
    bool queue_shift_past_end = false;
    // When a human player's base revolts under a police state, the faction it defects to is the nearest one with
    // diplomatic weighting, but the weighting uses the status with the best faction found so far (none at first)
    // instead of the faction of the base being measured. Fix: the status with that base's faction.
    bool revolt_weight_best_so_far = false;
    // When a revolting base joins another faction, both factions' free facilities are granted, but the technology
    // required for the new faction's ones is checked against the old faction's technologies. Fix: the new faction's
    // own technologies.
    bool capture_bonus_old_faction = false;
    // The advisor's request for more psych spending is skipped while the base builds a psych facility, or a Network
    // Node with the Virtual World; the Network Node test compares with unit 8 instead of the facility, so it never
    // matches and the request comes anyway. Fix: it compares with the Network Node.
    bool psych_request_unit_8 = false;
    // When the AI decides how much to hurry a Perimeter Defense or Tachyon Field, it checks whether an enemy faction
    // has bases on this base's continent, but reads the base count of the region numbered like the faction. Fix: the
    // enemy's bases in the region of this base.
    bool hurry_region_bases_index = false;
    // When fungus spreads from a base's ecological damage over several tiles, the search for the improvement it
    // destroys reuses the spreading loop's counter, which ends the spreading after the first tile. Fix: a separate
    // counter: the fungus spreads over all the tiles.
    bool fungus_spread_loop_counter = false;
    // ---- base_misc ----
    // The AI's choice of a unit to build weighs each candidate unit, but three adjustments meant for one candidate (a
    // sea-power faction's region count, a zeroed defence need for early formers, a cleared riot flag) are made to
    // variables shared by all candidates, so they also change the weights of every unit examined after it. Fix: the
    // adjustments are undone before the next candidate.
    bool base_build_persisting_locals = false;
    // When the AI rates a Subspace Generator (and a few other facilities) for a base, it reads the population of the
    // base whose id is the number of bases (a stale value: past the last base) instead of this base's. Fix: this
    // base's population.
    bool base_build_stale_base = false;
    // When the AI rates a Geosynchronous Survey Pod, its priorities (growth, tech, wealth, power) are meant to adjust
    // the facility's score, but adjust the stored id of the unit on the base's tile instead. Fix: they adjust the
    // score.
    bool base_build_stack_adjust = false;
    // ---- battle ----
    // The base defence bonus computed without an attacker (the defence strength shown for a base's units) uses level
    // -1, so a Tachyon Field (+2 levels) never counts there and the shown strength is lower than in a real attack.
    // Fix: the level of an ordinary attack (2) is used.
    bool level_without_attacker = false;
    // An artillery duel between a ship and a land unit is meant to be a single exchange of fire, but it ends early
    // only when the battle is shown on screen: a battle the player does not see is fought to the death, so the
    // outcome depends on whether the player could watch it. Fix: the exchange ends the same way whether shown or not;
    // only the drawing depends on the display.
    bool duel_ends_if_displayed = false;
    // ---- colortables ----
    // The RGB-to-HSV conversion used for the faction colour tables compares blue with the maximum instead of the
    // minimum, so colours whose blue is the smallest component get a wrong saturation and hue (slightly wrong unit
    // and border colours). Fix: blue is compared with the minimum.
    bool hsv_blue_minimum = false;
    // ---- console ----
    // Ordering a sea former to lower the sea floor at the lowest depth shows the SEATOOLOW message ("too low") but
    // gives the order anyway. Fix: the order is not given after the message.
    bool terraform_sea_too_low = false;
    // ---- console_mouse ----
    // While dragging a base's rally point, the drawn route's end point stores the tile's y as its x and leaves its y
    // stale, so the route line is drawn to a wrong spot. Fix: the end point is the tile.
    bool rally_leg_end = false;
    // ---- design ----
    // A save or map file whose header has 256 characters or more without an end leaves the header buffer unterminated
    // (the header test then reads past it). Fix: the header is cut at 255 characters.
    bool read_name_unterminated = false;
    // Automatic unit names: for a fast unit with the police ability, the alternative name's last character is to be
    // removed, but it is cut at the length of the (already shortened) main name, which can cut the alternative name
    // in the middle or past its end. Fix: the alternative name loses its own last character.
    bool alt_truncated_by_name = false;
    // When the AI prunes obsolete prototypes, it compares a design's range with the chassis of the design being added
    // instead of the design it is compared with, so designs are retired as obsolete wrongly (or kept). Fix: the range
    // of the design it is compared with.
    bool range_wrong_chassis = false;
    // ---- diplo_act ----
    // When a vendetta is declared, the "one side is human" test compares the two factions' bits of the human-players
    // mask in place (they sit at different bit positions), so it holds whenever either faction is human, also when
    // both are: the treaty flag and the "spoke" turn are set between two human players too. Fix: it holds when
    // exactly one of the two is human.
    bool human_bits_in_place = false;
    // When a base building a secret project is captured and another base of the old owner builds the same project,
    // the SURVIVEPROJECT message looks the project's name up with the negative production item (outside the facility
    // table): the original shows a garbled name. Fix: the project's name. (The reconstruction showed the name
    // natively already; the hybrid build keeps the original's read.)
    bool facility_negative_item = false;
    // The bit of a faction's secret project news (secret_project_intel) is addressed with a byte offset used as a
    // dword index. Not fixed: every place that sets or tests these bits uses the same mapping, and the 64 projects
    // fit the 8 dwords, so the bits are consistent and nothing goes wrong in play; changing the mapping would only
    // make saves disagree with the original.
    bool project_intel_byte_offset = true;
    // When a faction's headquarters base is captured, the headquarters escapes to its best remaining base for 1000
    // credits, but never to base 0 (the test is best > 0). Fix: base 0 can receive it too (best >= 0).
    bool headquarters_base_0 = false;
    // A captured base receives the free facilities of its new owner, and then also those of its old owner (judged
    // with the new owner's technologies): a capture can create facilities neither faction should get. Fix: only the
    // new owner's free facilities.
    bool capture_old_free_facilities = false;
    // After a base is captured from an AI faction, its other bases on that continent should reset their production
    // plans, but the region and faction arguments are swapped: the wrong bases (or none) are reset. Fix: the
    // arguments in their order (region, faction).
    bool bases_reset_args = false;
    // ---- diplo_comm ----
    // An AI's "you are in our territory" message (INOURTERRITORY2) names a base that may be none (-1): the name is
    // read from memory before the base table (garbage in the message). Fix: without a base the message is not shown.
    bool territory_base_minus1 = false;
    // When an AI offers to introduce the player to a faction it has met, the alien variant of the message is chosen
    // by the faction last asked for help instead of the faction introduced. Fix: by the faction introduced.
    bool introduce_alien_text = false;
    // When an AI reports on a war it is losing, the search for a continent where the player could help tests whether
    // any faction is on it, not the losing faction: the report can name a continent where the war is not fought. Fix:
    // the losing faction's bit.
    bool region_factions_bit = false;
    // ---- diplomacy ----
    // When an AI faction picks the home base a sea unit should return to, the coast test is made at the base whose id
    // is this base's x coordinate (and with this base's y as the radius flag), so ships are sent to inland bases or
    // kept from coastal ones. Fix: the base's own coast (is_coast(base.x, base.y, 0)).
    bool is_coast_base_x = false;
    // When the AI values a unit design offered in trade, it compares armour strengths computed from the weapon ids,
    // so the armour comparison is really a second weapon comparison. Fix: the armour ids.
    bool armor_from_weapon_ids = false;
    // Two AI diplomacy formulas (the tribute demanded, the price of a base) divide by a faction's power ranking,
    // which can be 0: the original crashes (division by zero). Fix: a ranking of 0 counts as 1 in the division.
    bool ranking_divide_zero = false;
    // A wealthy pact partner that gives energy away is meant to give an eighth of its treasury (25 to 500 credits),
    // but a max() where a min() was meant hands over its whole treasury. Fix: the gift is at most the treasury (the
    // intended eighth).
    bool gift_whole_treasury = false;
    // When an AI asks for two technologies in return for a deal, the second technology's name is written and then
    // discarded, so the request text does not name it ($TECH4 stays stale). Fix: the name goes into its own text slot
    // ($TECH4). (The game's text file labels this message GIMMETECH0 instead of GIMMETECH2, so the message itself may
    // still not appear.)
    bool trade_tech2_name_discarded = false;
    // When the player gives the AI a base, the thank-you message should vary with the gift's size (BASEGIFT1 /
    // BASEGIFT2), but the varied label is built in an unused buffer: the plain BASEGIFT message is always shown. Fix:
    // the built label is shown.
    bool basegift_label_unused = false;
    // The Peacekeepers' leader values technology bribes at 1.5 times, but the partial sums for one, two and three
    // technologies are all set to 1.5 times the scaled total of four, so the AI asks for fewer technologies than its
    // price needs. Fix: each partial sum is scaled by 1.5 on its own.
    bool peace_partial_sums = false;
    // ---- diplomacy_b ----
    // When two AI factions trade technologies, whether a technology may be passed on (not to a rival of the faction
    // it came from) reads the friction of the receiving faction with the receiver's source of the technology in both
    // directions. Fix: the giving faction's friction with its own source.
    bool tech_friction_receiver = false;
    // When the AI counts another faction's units trespassing on its territory, the base whose territory it is keeps
    // the previous unit's value (or the starting garbage) when the territory test does not set it, so units are
    // counted against the wrong base. Fix: the base starts as none (-1) for every unit.
    bool territory_base_id_stale = false;
    // After a human player's atrocities, the victim is to be marked as willing to retaliate in kind if it is an AI,
    // but the test uses a loop counter (8) instead of the victim: it is always true, also for human victims. Fix: the
    // victim's own human bit.
    bool atrocity_victim_loop_counter = false;
    // The atrocity message has a variant for when the viewer is the victim (ATROCITY1), but it is chosen when the
    // victim is the perpetrator itself (never), so the viewer never sees it. Fix: ATROCITY1 when the viewer is the
    // victim.
    bool atrocity_label_victim = false;
    // When the AI checks whether two factions' bases are connected over land, the faction id is passed as the unit
    // type to the path finder: for factions 3 and 4 that is a sea unit (transport foil, sea formers), so their land
    // connections are judged as sea routes. Fix: a land unit (scout patrol).
    bool path_unit_faction_id = false;
    // ---- diplowin ----
    // When the player proposes that an AI attack a faction, the "already at war with them" answer tests a loop
    // counter (8) as the faction, which reads the AI's agenda flags instead: the AI may refuse wrongly, or agree to
    // attack a faction it is already at war with. Fix: the AI's status with the faction to attack.
    bool diplo_agenda_loop_counter = false;
    // ---- heap ----
    // The game's string heaps grow with realloc, which can move the buffer: every string handed out before then
    // points into freed memory (garbled names or texts, or a crash, once a heap outgrows its first size). Fix: a
    // growable heap reserves 1 MB of room to grow in place, so its strings never move (past that, it grows as the
    // original's).
    bool heap_realloc_moves = false;
    // ---- infowin ----
    // When both panes of the info window are drawn at once, the line counter is not reset for the second pane, so its
    // first lines are skipped. Fix: each pane is drawn from its first line.
    bool infowin_counter_per_pane = false;
    // ---- map ----
    // In the scenario editor's debug mode, a tile's description should end with its region number, but the number is
    // appended to an unrelated text buffer instead. Fix: the number is appended to the tile's description.
    bool region_number_strbuffer = false;
    // clear_dirty() is meant to clear every map tile's redraw flag but sets it instead (the function is never called
    // by the game). Fix: it clears the flags.
    bool clear_dirty_sets = false;
    // Resizing the map (scenario editor) leaks the old map summary array (MapAbstract) each time. Fix: the old array
    // is freed first.
    bool map_resize_abstract_leak = false;
    // ---- move_order ----
    // An aircraft of the player that ends its move away from a base flies home unless it is carried; the "carried"
    // test checks whether the moving aircraft itself is a carrier, not whether a unit in its stack is, so aircraft on
    // a carrier fly off anyway (and a carrier never does). Fix: it tests each stack member's carrier ability.
    bool carried_ability_mover = false;
    // ---- parse ----
    // A text with a "$" marker (e.g. "$TEXT") not followed by any slot digit is scanned again and again: the game
    // hangs on such a text. Fix: the "$" is copied as it is and the scan goes on after it.
    bool parse_marker_no_digit = false;
    // ---- path ----
    // In the path finder's cost of a sea tile within reach of enemy artillery, the scan sets the direction loop's
    // counter to its end, so no further neighbour of the tile is examined (the code is only reached for sea units,
    // for which the path finder clears the flag, so it has no effect in play). Fix: the scan leaves the counter
    // alone.
    bool path_scan_dir_counter = false;
    // When a unit's path step is blocked by a zone of control, the exception for moving next to a native base tests
    // the tile at index x / 2 (a tile of the map's first row) instead of the target tile. Fix: the target tile.
    bool native_base_row_0 = false;
    // ---- prefs ----
    // While the preferences load, the default preference bits are written after a leftover "0" in the shared text
    // buffer. (A leading zero of a binary number changes nothing, so the bug has no visible effect.) Fix: the buffer
    // is cleared after the laptop setting is read.
    bool laptop_prefs_strbuffer = false;
    // ---- probe ----
    // The probe team odds shown before an action (and returned to the caller): against a target with the
    // Hunter-Seeker Algorithm the success chance is halved twice, while the probe's survival chance is never halved.
    // Fix: the success chance is halved once and the survival chance once.
    bool probe_survival_halved = false;
    // When an AI probe team picks which captured faction leader to free, the comparison between candidates reads its
    // own faction's diplomatic status with the faction after the candidate (freed + 1), so it may prefer the wrong
    // leader. Fix: the status with the candidate faction itself.
    bool probe_freed_status_offset = false;
    // ---- proto ----
    // When a new prototype is designed, the game checks whether existing units already have components as good; the
    // extra "prototyped from a flagged unit" mark (unit flag 0x10) is taken from whichever unit the loop looked at
    // last, not from a unit that matched. Not fixed: the components are matched by different units, so there is no
    // single "matching" unit, and the meaning of flag 0x10 is unknown: the intended rule cannot be recovered.
    bool prototyped_last_unit = true;
    // ---- rules ----
    // When a faction file names the social effect its AI emphasises (the SOCIAL line's effect, e.g. ECONOMY), the
    // matching index is found but -1 is stored, so no faction has an emphasised effect. Fix: the matching effect's
    // index is stored.
    bool social_effect_not_stored = false;
    // ---- save ----
    // Encrypting a save block of an odd size reads one byte past its end as the padding byte (every block the game
    // writes has an even size, so it has no effect). Fix: the padding byte is 0.
    bool encrypt_odd_size = false;
    // When a save from an old version (before version 11) is loaded, each faction's facility-announced bytes are
    // converted keeping only 5 of the 8 old bytes, and 3 of the new bytes keep stale high bytes of another field.
    // Fix: all 8 bytes are copied and the 4 new ones cleared.
    bool faction_convert_facility_bytes = false;
    // When a save from an old version (before version 11) is loaded, each base's facility bytes keep only 5 of the 8
    // old bytes (facilities 40-63 are lost), 3 of the new bytes keep stale high bytes, and a padding field keeps half
    // of another value. Fix: all 8 bytes are copied and the new bytes and the padding cleared.
    bool base_convert_facility_bytes = false;
    // Loading a PBEM save whose stored diplomatic messages include an empty text fails (reading 0 bytes counts as a
    // read error), so the save cannot be loaded. Fix: an empty text is not read.
    bool empty_text_fails_load = false;
    // Saving writes the old-format preferences block from uninitialised memory (only its version field is used when
    // loading). Fix: the block is written as zeros (with the version, and smacx's own mark of the save's
    // original-bugs setting). The reconstruction wrote zeros already, so both settings play alike.
    bool legacy_prefs_uninit = false;
    // Loading the map of a savegame (the scenario editor's map loading from a .SAV) skips the blocks before the map
    // with the layout of a very old format, so the map is read from the wrong place in the file (a garbled map or a
    // failed load). Fix: the blocks are skipped as the savegame lays them out (old and current versions, encrypted or
    // not).
    bool map_data_save_layout = false;
    // Saving a map file calls fclose() also when the file could not be created (a null file: the original can crash).
    // Fix: the file is closed only when it was opened.
    bool fclose_null_file = false;
    // The scenario editor's undo files are saved with the .SAV extension but loaded, deleted and rotated without it,
    // so undo never finds them and old ones pile up. Fix: the names with .SAV everywhere.
    bool undo_name_extension = false;
    // When a new undo file is saved, the older ones should move up one slot, but each rename moves the higher slot
    // onto the lower one, so the slots do not rotate and earlier states are lost. Fix: each slot moves up to the next
    // one before the new state is saved in slot 1.
    bool undo_rotation = false;
    // ---- score ----
    // transcending() should tell whether a faction is building the Ascent to Transcendence, but compares the
    // production queue with +102 (a unit id) instead of -102 (the secret project), so it is never true: the AI does
    // not react to a rival's Ascent. Fix: it compares with the project (-FAC_ASCENT_TO_TRANSCENDENCE).
    bool ascent_queue_sign = false;
    // The faction power ranking ("might", used by AI diplomacy and the score graphs) ignores unit types of which a
    // faction has 251 or more units, and the byte-sized per-type count wraps at 256. Fix: the units are counted from
    // the vehicle list, without a limit.
    bool unit_count_251 = false;
    // In the faction power ranking a unit's value is scaled by its weapon strength over the best weapon, but the
    // integer quotient is taken first: every unit weaker than the best weapon counts 0. Fix: the value is multiplied
    // by the weapon strength first, then divided.
    bool weapon_ratio_integer = false;
    // The score's Transcendence bonus divides by the total population of the ascending faction and its pact partners,
    // which can be 0 (division by zero: the original crashes). Fix: the bonus is 0 then. The reconstruction did this
    // already, so both settings play alike.
    bool score_divide_zero = false;
    // The end-of-game replay records base founding and capture events without a limit: past 1024 events the records
    // overwrite the game data after the replay table. Fix: events past the table's end are not recorded.
    bool replay_events_overflow = false;
    // ---- setup ----
    // When a faction lands (or an eliminated AI faction respawns), the search for its best landing tile requires a
    // distance of 6 from every unit, but measures it from the search's centre instead of from each candidate tile, so
    // all candidates are judged by the same distance. Fix: the distance from each candidate tile.
    bool setup_site_distance_origin = false;
    // An aquatic AI faction gets a sea colony pod prototype in its first free prototype slot; with no free slot the
    // prototype overwrites the unit whose id happens to be in killer_id (another unit design, possibly another
    // faction's). Fix: no prototype is made when there is no free slot.
    bool sea_pod_slot_killer_id = false;
    // Time warp ("start with bases" setups) means to give a growth-minded AI faction's first base a Perimeter
    // Defense, but the arguments are swapped: facility 1 (Headquarters) is set or cleared at base 4 instead. Fix: the
    // Perimeter Defense is set at the faction's first base.
    bool perimeter_defense_args = false;
    // Time warp gives each AI faction's first base a random secret project among the first eight; when every eligible
    // one is already built the random draw never ends and the game hangs (possible only when time warp runs on a game
    // in progress). Fix: no project is given when none is left.
    bool time_warp_project_loop = false;
    // Time warp's search for more base sites near a foreign base compares the tile's region with itself, so every
    // site near another faction's base is skipped, also on another continent. Fix: a site is skipped when it is in
    // the region of that foreign base, and only valued at half otherwise.
    bool setup_region_self_compare = false;
    // Time warp: when a faction's smallest base grows instead of founding a new one, its new Mind Worms escort should
    // get Disciplined morale, but the morale goes to the unit created before it. Fix: the new unit gets the morale.
    bool setup_morale_last_veh = false;
    // Time warp: an aquatic faction's extra sea escape pod for its smallest base is placed at the last base the site
    // search looked at, not at that base. Fix: it is placed at the base.
    bool setup_escape_pod_location = false;
    // ---- stack ----
    // When checking whether a sensor sees a tile, the game looks for the base nearest to the unit's own tile rather
    // than the sensor's tile, so a base's sensor-related coverage (facility 41 next to the base) is tested at the
    // wrong place. Fix: the base nearest to the sensor tile, and the distance from it.
    bool sensor_base_nearest_unit = false;
    // When a unit spots an enemy unit, the decision to redraw the map tests whether the spotting unit was already
    // visible, not the spotted one: the newly seen unit may not be drawn until the next redraw. Fix: the spotted
    // unit's visibility.
    bool spot_visibility_spotter = false;
    // In network games stack_fix() should move the units of one tile to the top of the stack list, but tests the
    // position of the given unit for every unit, so every unit on the map is promoted and re-sorted. Fix: only the
    // units on that tile.
    bool promote_position_veh_id = false;
    // Loading units onto a transport reads the map tile of the transport (or of the cargo) without checking that the
    // unit is on the map; a unit off the map (e.g. a unit in transit, at -2, -2) reads memory before or after the
    // map. Fix: a unit off the map is treated as not at a base, not at an airbase and not in a region with a plan.
    bool stack_map_bounds = false;
    // ---- strings ----
    // purge_leading() is meant to remove leading spaces from a string but discards its result, so the string keeps
    // its spaces. Fix: the text after the spaces is moved to the start of the string.
    bool purge_leading_noop = false;
    // ---- tech ----
    // has_tech() refuses the last entry of the technology table (id 88), so a faction never counts as owning it, even
    // after researching it: anything that requires it stays unavailable. Fix: has_tech() accepts every id up to 88.
    bool last_tech_never_counts = false;
    // ---- text ----
    // Reading a text string at the end of a text file stores a null string (the original reads memory address 0,
    // which on Windows gives an empty or invalid entry). Fix: the empty string is stored.
    bool text_null_string = false;
    // ---- turn ----
    // The random events that destroy a sea base's kelp farms or tidal platforms require a tile that is ocean and land
    // at once, so they never happen. Fix: an ocean tile is enough: the events happen as their messages (KELPWIPE,
    // PLATFORMWIPE) describe.
    bool sea_wipe_never = false;
    // The plague random event's message is shown for base 0 instead of the stricken base when only that base is
    // visible to the player (no own base affected). Fix: the message names the stricken base.
    bool event_shown_base_0 = false;
    // When a random event destroys farms and forests, the tile memory of the player's own faction is cleared seven
    // times instead of every faction's once: other factions keep seeing the destroyed farms (and with viewer 0, the
    // tile's landmark bits are cleared instead). Fix: every faction's memory of the tile is cleared.
    bool landmark_memory_viewer = false;
    // When forests or kelp spread to a neighbouring tile, the factions that see the old tile (and remember its
    // growth) learn of the new one, not those that see the new tile; the message and redraw use the old tile, and a
    // new mind worm on fungus redraws a stale tile. Fix: the factions that see the new tile learn of it, and the new
    // tiles are drawn.
    bool spread_visibility_source = false;
    // Code after the native life loop (mind worms attacking the area of a base, with the XMINDWORMS message) is
    // unreachable: the loop never exits early. Not fixed: the code was never part of the game's play and its intended
    // trigger is unknown; making it reachable would add a feature (worm attacks on bases) rather than fix one.
    bool fungal_worms_unreachable = true;
    // At the start of a turn, an aircraft out of its base with moves left makes turn_upkeep() return early, skipping
    // the rest of the turn's upkeep (bases, research, the turn counter...) until no such aircraft remains; the
    // aircraft's fuel counter also goes up again at each repeat. Fix: the aircraft's fuel counter goes up and the
    // upkeep goes on with the next unit.
    bool turn_upkeep_air_return = false;
    // The AI's adjustment of its psych allocation tests the base selected last (CurrentBase) for drone riots, once
    // per base, instead of each of its bases: riots in its other bases do not raise the psych spending. Fix: each
    // base of the faction is tested.
    bool drone_riot_current_base = false;
    // In Planetary Council elections an AI favours a candidate by mixing ranks and faction ids: it looks for the
    // highest rank whose faction is AI and checks its diplomatic status with the faction whose id is that rank
    // number, then favours the candidate whose id equals the rank. Fix: it favours the highest-ranked AI faction it
    // is not at war with (or hostile to).
    bool council_rank_faction_mix = false;
    // After a human player's turn ends with a saved game loaded, the faction round restarts at faction 1 instead of
    // faction 0. Not fixed: it is harmless: the restart is how the round resumes at the loaded game's player
    // (factions before it are skipped), which skips faction 0 anyway; going on with the next faction instead would be
    // wrong.
    bool net_round_restart = true;
    // ---- veh ----
    // A new AI unit created on the continent of its faction's attack target picks the enemy with the most bases
    // there, but compares its own faction's base count on that continent for every enemy, so it always picks the
    // first enemy at war. Fix: each enemy's own base count.
    bool enemy_region_own_count = false;
    // ---- vehdraw ----
    // When the unit sprites are loaded, the vertical position of the faction flag markers is stored in the slot of a
    // stale loop counter, while the drawing reads slot 0 (which stays 0): flags are drawn at the top of the sprite.
    // Fix: the position goes to slot 0, which the drawing reads.
    bool flag_marker_stale_slot = false;
    // A unit's 3D model draws the faction's free abilities that the unit already has a second time (the ability
    // models overlap). Fix: the free abilities are drawn only when the unit does not have them already.
    bool free_abilities_twice = false;
    // ---- voxel_render ----
    // The 3D unit renderer computes the y axis length (used for the shadow) from matrix elements 4 and 5 instead of 3
    // and 5 as for the x axis (elements 0 and 2), which distorts the shadows of rotated units. Fix: elements 3 and 5.
    bool voxel_y_axis_elements = false;
    // ---- world ----
    // When a saved map's altitudes are converted to the linear scale (on loading), the highest altitude band (6) is
    // scaled towards entry 10 of the altitude table instead of entry 7, so the highest mountains get altitudes above
    // the top of the scale. Fix: every band, the highest included, is scaled towards the next entry (alt + 1).
    bool alt_upper_bound_6 = false;
    // Emulated stack residues and uninitialised reads of the original (docs/RECONSTRUCTION.md,
    // "Known uninitialised reads"). The native values live in g_stack_residue (stack_residue.h),
    // enemy_move's slots in its EM frame (ai_move.cpp), order_veh's in OrderVeh (move_order.cpp).
    struct StackResidue {
        // An alien faction's base energy bonus (from its facilities and secret projects) starts from a variable that
        // only the trade loop sets; for a base without trade partners it starts from whatever the mineral computation
        // left there, so the bonus varies at random. Fix: the bonus starts from 0.
        bool alien_bonus_energy = false;
        // base_rank() (the base of a given energy rank among a faction's bases) reads its own sort buffer's values as
        // base ids for a negative rank (the game never passes one). Fix: a negative rank gives no base (-1).
        bool base_list_frame = false;
        // In a revolt, the shares of factions without a REVOLT bonus are never set (stack memory). Fix: they are 0.
        // The reconstruction used 0 already (the value the original's memory held in every observed game), so both
        // settings play alike.
        bool revolt_shares = false;
        // Path_move() starts the path finder with a zone-of-control flag taken from stale stack memory (what speed()
        // left), which changes the costs of the destination's neighbours. Fix: the flag starts as 0, as for every
        // other path search.
        bool path_start_zoc = false;
        // When an AI faction considers calling the Planetary Council, its "aliens alive" flag is only computed when
        // the Supreme Leader proposal is considered; otherwise it holds a stale processor register, which in any game
        // with a human player blocks every proposal but governor elections: AI factions never propose anything else.
        // Fix: the flag is computed for every proposal (AI factions then propose council motions as in games without
        // humans, unless an alien faction is alive).
        bool council_aliens_ecx = false;
        // A native unit's (Isle of the Deep) movement code reads a score variable before setting it. Fix: it starts
        // as 0. The reconstruction used 0 already (the value the original's memory held in every observed game), so
        // both settings play alike.
        bool ai_score = false;
        // A text's "$<#n:...>" form (choose a word by a number's plurality) reads a gender variable that it never
        // sets: the word chosen for a feminine or neuter form depends on what an earlier text left there (the first
        // time, on stale memory). Fix: the gender starts as 0 (masculine). The reconstruction used 0 already (the
        // value the original's memory held in every observed game), so both settings play alike.
        bool parse_gender = false;
        // wants_to_attack() (an AI's decision to attack a faction) takes the largest number of bases each faction has
        // on one continent, starting from stack memory the function never sets; in a pair of calls the second sees
        // the first's values, so the decision depends on earlier calls. Fix: the maxima start from 0 at every call.
        bool region_maxima = false;
        // An AI unit's choice of a fungus tile next to a base reads a distance variable not set on this path in the
        // same move. Fix: it reads 0. The reconstruction's move frame starts at 0 (the value of the original's
        // poisoned stack in every observed game), so both settings play alike.
        bool enemy_move_s38 = false;
        // An AI transport deciding which sentried passengers to keep compares with a count variable that most paths
        // do not set in the same move. Fix: it reads 0. The reconstruction's move frame starts at 0 (the value of the
        // original's poisoned stack in every observed game), so both settings play alike.
        bool enemy_move_s48 = false;
        // An AI escort ship heading for a transport chooses its next step by a distance variable that is not set when
        // no transport was examined in the same move. Fix: it reads 0. The reconstruction's move frame starts at 0
        // (the value of the original's poisoned stack in every observed game), so both settings play alike.
        bool enemy_move_edi = false;
        // After a won attack, or a move that ends without a direction, the unit's post-move code reads the "move is
        // visible to the player" flag that these paths never set (after an attack it holds an unrelated stack value):
        // map scrolling, booms, and messages such as PLANECRASH depend on stale memory. Fix: the flag is computed as
        // for any other move.
        bool order_veh_visible = false;
    } residue;
};

static_assert(std::is_trivially_copyable<Compat>::value, "Compat holds only bools");

inline Compat g_compat;

// The setting the game plays with: true the original bugs, false the fixes (the default).
inline bool g_original_bugs = false;
// The setting was given on the command line (smacx --original-bugs, smacx_headless
// --original-bugs): a loaded multiplayer save does not change it. (An online game's host
// always decides for its players.)
inline bool g_original_bugs_forced = false;

// Plays with the original bugs (on) or with the fixes (off): every flag true, or every flag at
// its default (the flags whose fix is not written stay true).
inline void compat_set_original_bugs(bool on) {
    g_original_bugs = on;
    if (on) std::memset(static_cast<void*>(&g_compat), 1, sizeof(g_compat));
    else g_compat = Compat{};
}

// A COMPAT_ORIGINAL site was reached with its flag switched off: no fixed path exists.
[[noreturn]] inline void compat_unfixed(const char* flag) {
    std::fprintf(stderr, "compat: %s has no fixed path (src/engine/compat.h)\n", flag);
    std::abort();
}

// Marks a site that keeps a bug of the original without a fix (see the flag's "Not fixed:").
#define COMPAT_ORIGINAL(flag) (g_compat.flag ? (void)0 : compat_unfixed(#flag))
