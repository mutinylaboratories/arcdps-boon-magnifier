#pragma once
#include "arcdps_defs.hpp"
#include "boon_tracker.hpp"
#include "boons.hpp"

namespace core {

// Routes arcdps area-combat callbacks for one boon on the local player into a
// BoonTracker. Understands both the statechange-based buff events
// (CBTS_BUFFAPPLY & co.) and the older is_buffremove/is_offcycle encoding.
class BoonEventRouter {
public:
    BoonEventRouter(BoonTracker& tracker, uint32_t buff_id)
        : tracker_(tracker), buff_id_(buff_id) {}

    // age_ms: how long ago the event happened (arcdps delivers asynchronously).
    void handle(const cbtevent* ev, const ag* src, const ag* dst,
                uint64_t now_ms, int64_t age_ms);

private:
    bool is_self(const ag* a, uint64_t agent_id) const;

    BoonTracker& tracker_;
    uint32_t buff_id_;
    uint64_t self_id_ = 0;
};

}  // namespace core
