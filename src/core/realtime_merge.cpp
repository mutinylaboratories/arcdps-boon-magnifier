#include "realtime_merge.hpp"
#include <algorithm>

namespace core {

BoonSnapshot merge_realtime(const BoonSnapshot& arcdps, const RealtimeState& rt, uint64_t now_ms,
                            uint64_t stale_after_ms) {
    if (!rt.valid) return arcdps;
    if (stale_after_ms && now_ms > rt.updated_ms && now_ms - rt.updated_ms > stale_after_ms) return arcdps;

    BoonSnapshot out;
    out.active = rt.active;
    if (!rt.active) return out;   // stripped/expired: drop immediately, whatever arcdps still believes

    out.stacks = rt.stacks > 0 ? rt.stacks : std::max(arcdps.stacks, 1);
    if (rt.remaining_ms >= 0) {
        // The source reported remaining_ms at updated_ms; keep it ticking between reports.
        int64_t elapsed = now_ms > rt.updated_ms ? (int64_t)(now_ms - rt.updated_ms) : 0;
        out.remaining_ms = std::max<int64_t>(0, rt.remaining_ms - elapsed);
        out.peak_ms = std::max(arcdps.peak_ms, rt.remaining_ms);
    } else if (arcdps.active) {
        out.remaining_ms = arcdps.remaining_ms;
        out.peak_ms = arcdps.peak_ms;
    } else {
        out.remaining_known = false;   // up, but arcdps hasn't delivered the duration yet
    }
    return out;
}

}  // namespace core
