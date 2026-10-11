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

// The game stores "permanent" as a 0xFFFFFFFF ms duration (its tooltip shows it as ~49 days).
// Anything this long or longer is treated as permanent by the overlay (drawn as an infinity).
constexpr int64_t kPermanentMs = 7LL * 24 * 3600 * 1000;

// Time left on one stack from the game's 32-bit timeGetTime() clock: started_ms + duration_ms
// - now_ms, wrap-safe, never negative, and a permanent duration stays permanent regardless of
// how long ago it started (it would otherwise read as expired after 24.8 days of uptime).
int64_t stack_remaining_ms(uint32_t started_ms, uint32_t duration_ms, uint32_t now_ms);

}  // namespace core
