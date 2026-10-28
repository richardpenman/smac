// Engine functions that the `call` script command can run (harness/README.md, "Setup commands").
// Shared by harness/oracle/oracle.cpp (calls the original address: in hybrid.dll that reaches the
// reconstruction when the function is patched) and src/app/headless.cpp (calls the
// reconstruction). Only cdecl functions with integer arguments; at most 8 arguments.
//
// SCRIPT_CALL(name, original address, returns a value: 1, void: 0, the first argument is a
// vehicle id: 1 (the call logs a warning when it is not below VehCount))
SCRIPT_CALL(veh_init, 0x5C03D0, 1, 0)
SCRIPT_CALL(veh_kill, 0x5C08C0, 0, 1)
SCRIPT_CALL(veh_put, 0x5A59B0, 0, 1)
SCRIPT_CALL(tech_achieved, 0x5BB000, 0, 0)
SCRIPT_CALL(set_treaty, 0x55BB30, 0, 0)
SCRIPT_CALL(set_fac, 0x4E48B0, 0, 0)
SCRIPT_CALL(goody_box, 0x57A660, 1, 0)
SCRIPT_CALL(give_a_base, 0x54CF40, 0, 0)
SCRIPT_CALL(probe, 0x59F120, 1, 1)
SCRIPT_CALL(enemy_diplomacy, 0x55F930, 0, 0)
SCRIPT_CALL(action_staple, 0x4CA7F0, 0, 0)
SCRIPT_CALL(action_feature, 0x4CD060, 0, 0)
SCRIPT_CALL(action_arty, 0x4CD090, 0, 1)
SCRIPT_CALL(action_destruct, 0x4CD2F0, 0, 1)
SCRIPT_CALL(action_oblit, 0x4CD4E0, 0, 1)
SCRIPT_CALL(valid_patrol, 0x4CD6A0, 1, 1)
SCRIPT_CALL(action_patrol, 0x4CDA30, 0, 1)
SCRIPT_CALL(action_tectonic, 0x4CE210, 0, 1)
SCRIPT_CALL(action_fungal, 0x4CE790, 0, 1)
SCRIPT_CALL(fungal_tile, 0x4CE6B0, 0, 0)
SCRIPT_CALL(action_give, 0x4CEDE0, 0, 1)
SCRIPT_CALL(action_gate, 0x4CF380, 0, 1)
SCRIPT_CALL(action_sat_attack, 0x4CF480, 0, 0)
