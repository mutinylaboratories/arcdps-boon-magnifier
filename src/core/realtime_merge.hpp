#pragma once
#include <cstdint>
#include "boon_tracker.hpp"

// Combines a realtime "is the boon up right now" source with arcdps' delayed but
// exact durations. arcdps delivers boon events ~2.65s late, so on its own the overlay
// appears late and lingers after a strip; a realtime source fixes presence, arcdps
// (when it has caught up) supplies the countdown.
namespace core {

struct RealtimeState {
    bool    valid = false;        // a realtime source is connected and has reported at least once
    bool    active = false;
    int     stacks = 0;           // 0 = source doesn't know the stack count
    int64_t remaining_ms = -1;    // -1 = source doesn't know the duration
    uint64_t updated_ms = 0;      // when the source last reported
};

// stale_after_ms: a polling source that stops reporting is ignored after this long (0 = never stale,
// for event-driven sources).
BoonSnapshot merge_realtime(const BoonSnapshot& arcdps, const RealtimeState& rt, uint64_t now_ms,
                            uint64_t stale_after_ms);

}  // namespace core
