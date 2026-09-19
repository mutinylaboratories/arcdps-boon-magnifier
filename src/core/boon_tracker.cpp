#include "boon_tracker.hpp"
#include <algorithm>
#include <cstdlib>

namespace core {

void BoonTracker::advance(uint64_t now_ms) {
    if (last_ms_ == 0 || now_ms <= last_ms_) {
        if (now_ms > last_ms_) last_ms_ = now_ms;
        return;
    }
    int64_t elapsed = (int64_t)(now_ms - last_ms_);
    last_ms_ = now_ms;

    if (stacking_ == Stacking::Intensity) {
        for (auto& s : stacks_) s.remaining_ms -= elapsed;
        stacks_.erase(std::remove_if(stacks_.begin(), stacks_.end(),
                                     [](const Stack& s) { return s.remaining_ms <= 0; }),
                      stacks_.end());
    } else {
        // Only the front stack ticks; leftover time spills into the next one.
        while (elapsed > 0 && !stacks_.empty()) {
            Stack& front = stacks_.front();
            if (front.remaining_ms > elapsed) { front.remaining_ms -= elapsed; break; }
            elapsed -= front.remaining_ms;
            stacks_.erase(stacks_.begin());
        }
    }
    if (stacks_.empty()) peak_ms_ = 0;
}

BoonTracker::Stack* BoonTracker::find(uint32_t id) {
    if (id == 0) return nullptr;
    for (auto& s : stacks_) if (s.id == id) return &s;
    return nullptr;
}

int64_t BoonTracker::total_remaining() const {
    int64_t r = 0;
    for (const auto& s : stacks_)
        r = (stacking_ == Stacking::Intensity) ? std::max(r, s.remaining_ms) : r + s.remaining_ms;
    return r;
}

void BoonTracker::apply(uint32_t id, int64_t duration_ms, uint64_t now_ms) {
    advance(now_ms);
    if (duration_ms <= 0) return;
    if (Stack* s = find(id)) {
        s->remaining_ms = duration_ms;
    } else {
        if ((int)stacks_.size() >= max_stacks_) {
            // Game replaces the shortest stack when capped.
            auto shortest = std::min_element(stacks_.begin(), stacks_.end(),
                [](const Stack& a, const Stack& b) { return a.remaining_ms < b.remaining_ms; });
            if (shortest->remaining_ms >= duration_ms) return;
            stacks_.erase(shortest);
        }
        stacks_.push_back({id, duration_ms});
    }
    peak_ms_ = std::max(peak_ms_, total_remaining());
}

void BoonTracker::change(uint32_t id, int64_t delta_ms, int64_t new_duration_ms, uint64_t now_ms) {
    advance(now_ms);
    Stack* s = find(id);
    if (!s) {
        // Missed the original application; adopt the stack if we know its length.
        if (new_duration_ms > 0) apply(id, new_duration_ms, now_ms);
        return;
    }
    s->remaining_ms = new_duration_ms > 0 ? new_duration_ms : s->remaining_ms + delta_ms;
    if (s->remaining_ms <= 0) {
        stacks_.erase(stacks_.begin() + (s - stacks_.data()));
        if (stacks_.empty()) peak_ms_ = 0;
        return;
    }
    peak_ms_ = std::max(peak_ms_, total_remaining());
}

bool BoonTracker::set_duration(uint32_t id, int64_t duration_ms, uint64_t now_ms) {
    advance(now_ms);
    Stack* s = find(id);
    if (!s || duration_ms <= 0) return false;
    s->remaining_ms = duration_ms;
    peak_ms_ = std::max(peak_ms_, total_remaining());
    return true;
}

void BoonTracker::remove_single(uint32_t id, int64_t removed_ms, uint64_t now_ms) {
    advance(now_ms);
    if (stacks_.empty()) return;
    size_t idx;
    if (Stack* s = find(id)) {
        idx = (size_t)(s - stacks_.data());
    } else {
        // Unknown id: drop whichever stack best matches the removed duration.
        idx = 0;
        int64_t best = INT64_MAX;
        for (size_t i = 0; i < stacks_.size(); ++i) {
            int64_t d = std::llabs(stacks_[i].remaining_ms - removed_ms);
            if (d < best) { best = d; idx = i; }
        }
    }
    stacks_.erase(stacks_.begin() + idx);
    if (stacks_.empty()) peak_ms_ = 0;
}

void BoonTracker::remove_all() {
    stacks_.clear();
    peak_ms_ = 0;
}

BoonSnapshot BoonTracker::snapshot(uint64_t now_ms) {
    advance(now_ms);
    BoonSnapshot out;
    out.stacks = (int)stacks_.size();
    out.active = out.stacks > 0;
    out.remaining_ms = total_remaining();
    out.peak_ms = peak_ms_;
    return out;
}

}  // namespace core
