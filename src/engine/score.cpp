// Objectives, score and replay (original module around 0x5ABFF0-0x5AE120).
#include "engine/score.h"

#include <cstring>

#include "engine/log.h"

void GAMEAPI replay_base(int32_t event, int32_t x, int32_t y, int32_t faction_id) {
    log_say_2("Replay Base", x, y, faction_id);
    log_say_2("Replay Base Info", CurrentTurn, event, ReplayEventSize);
    ReplayEvent e;
    e.event = (int8_t)event;
    e.faction_id = (int8_t)faction_id;
    e.turn = (int16_t)CurrentTurn;
    e.x = (int16_t)x;
    e.y = (int16_t)y;
    // BUG(original): no bounds check; past 1024 events the record is written beyond ReplayEvents.
    std::memcpy(reinterpret_cast<uint8_t*>(ReplayEvents) + ReplayEventSize, &e, sizeof(e));
    ReplayEventSize += 8;
}
