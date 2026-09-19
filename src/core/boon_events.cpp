#include "boon_events.hpp"
#include <cstring>

namespace core {

static uint32_t trackable_id(const cbtevent* ev) {
    uint32_t id;
    std::memcpy(&id, &ev->pad61, sizeof(id));
    return id;
}

bool BoonEventRouter::is_self(const ag* a, uint64_t agent_id) const {
    if (a && a->self) return true;
    return self_id_ != 0 && agent_id == self_id_;
}

void BoonEventRouter::handle(const cbtevent* ev, const ag* src, const ag* dst,
                             uint64_t now_ms, int64_t age_ms) {
    if (!ev) {
        // Agent tracking notification: src->prof set means "added", dst carries the self flag.
        if (src && dst && src->prof && dst->self) self_id_ = src->id;
        return;
    }
    if (age_ms < 0) age_ms = 0;

    switch (ev->is_statechange) {
    case CBTS_MAPCHANGE:
        tracker_.remove_all();
        return;

    case CBTS_BUFFINITIAL:
    case CBTS_BUFFAPPLY:
        if (ev->skillid == buff_id_ && is_self(dst, ev->dst_agent))
            tracker_.apply(trackable_id(ev), (int64_t)ev->value - age_ms, now_ms);
        return;

    case CBTS_BUFFCHANGE:
        if (ev->skillid == buff_id_ && is_self(dst, ev->dst_agent))
            tracker_.change(trackable_id(ev), ev->value,
                            (int64_t)ev->overstack_value - age_ms, now_ms);
        return;

    case CBTS_BUFFREMOVE_SINGLE:
        if (ev->skillid == buff_id_ && is_self(src, ev->src_agent))
            tracker_.remove_single(trackable_id(ev), ev->value, now_ms);
        return;

    case CBTS_BUFFREMOVE_ALL:
        if (ev->skillid == buff_id_ && is_self(src, ev->src_agent))
            tracker_.remove_all();
        return;

    // Per-stack duration updates. Instance ids get reused across buffs, so only trust
    // these when the event names our buff (live logs show some arrive without a skill id).
    case CBTS_BUFFACTIVE:
        if (ev->skillid == buff_id_ && is_self(src, ev->src_agent))
            tracker_.set_duration((uint32_t)ev->dst_agent, (int64_t)ev->value - age_ms, now_ms);
        return;
    case CBTS_BUFFDEACTIVE:
        if (ev->skillid == buff_id_ && is_self(src, ev->src_agent))
            tracker_.set_duration(trackable_id(ev), (int64_t)ev->value - age_ms, now_ms);
        return;

    case CBTS_NONE:
        break;
    default:
        return;
    }

    // Legacy encoding: buff events ride on plain combat events.
    if (ev->skillid != buff_id_ || !ev->buff || ev->buff_dmg != 0 || ev->is_activation) return;

    if (ev->is_buffremove != CBTB_NONE) {
        if (!is_self(src, ev->src_agent)) return;
        if (ev->is_buffremove == CBTB_ALL) tracker_.remove_all();
        else tracker_.remove_single(trackable_id(ev), ev->value, now_ms);
        return;
    }
    if (!is_self(dst, ev->dst_agent)) return;
    if (ev->is_offcycle)   // extension: value is the delta, overstack_value the new length
        tracker_.change(trackable_id(ev), ev->value, (int64_t)ev->overstack_value - age_ms, now_ms);
    else
        tracker_.apply(trackable_id(ev), (int64_t)ev->value - age_ms, now_ms);
}

}  // namespace core
