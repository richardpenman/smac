/* Generated: engine functions that read timeGetTime(). Outside the logic-clock sites these
 * reads only pace animations/delays, so the oracle returns a fast-forwarding clock to them. */
static const uint32_t fast_clock_funcs[][2] = {
    {0x4ECBF0, 0x4EEF61}, /* base_terraform */
    {0x4F4FB0, 0x4F5CD6}, /* drone_riot */
    {0x504AA0, 0x505D1B}, /* boom */
    {0x50DD90, 0x50DDBA}, /* clock_wait */
    {0x522180, 0x522EFC}, /* alien_fauna */
    {0x528EA0, 0x529097}, /* net_upkeep_phase */
    {0x529F30, 0x52A2D3}, /* net_end_of_turn */
    {0x539820, 0x5398BF}, /* diplo_lock */
    {0x53E5C0, 0x53E7F8}, /* renounce_pact */
    {0x543BC0, 0x544E36}, /* propose_pact */
    {0x544E40, 0x545D55}, /* propose_treaty */
    {0x545E80, 0x547C08}, /* propose_attack */
    {0x547C10, 0x548DC8}, /* make_gift */
    {0x549930, 0x54ACB9}, /* threaten */
    {0x54D410, 0x54DC8C}, /* base_swap */
    {0x54FFD0, 0x558653}, /* communicate */
    {0x55A150, 0x55AF11}, /* veh_scoot */
    {0x592E10, 0x592E70}, /* message_veh */
    {0x592E70, 0x592EDA}, /* message_base */
    {0x592EE0, 0x592F4C}, /* message_data */
    {0x592F50, 0x592FE9}, /* FUN_00592f50 */
    {0x592FF0, 0x593089}, /* message_big_data */
    {0x593090, 0x593108}, /* message_lock */
    {0x593110, 0x593190}, /* message_landmark */
    {0x593190, 0x593215}, /* message_chat */
    {0x59F120, 0x5A5837}, /* probe */
    {0x5A94F0, 0x5A96C3}, /* save_daemon */
    {0x5AE120, 0x5AED43}, /* crash_landing */
    {0x5C5A30, 0x5C5ADB}, /* world_climate */
    {0x5C7020, 0x5C753B}, /* world_borehole */
};
