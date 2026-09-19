#pragma once
#include <cstdint>
#include <vector>

// Host-independent boon stack bookkeeping. Fed by arcdps buff events, read by
// the overlay. Not thread-safe; the plugin layer wraps it in a mutex.
namespace core {

enum class Stacking {
    Intensity,   // every stack ticks at once (stability, might)
    Duration,    // only the front stack ticks, the rest queue (protection, fury)
};

struct BoonSnapshot {
    bool    active = false;
    int     stacks = 0;
    int64_t remaining_ms = 0;   // time until the boon is completely gone
    int64_t peak_ms = 0;        // largest remaining_ms seen during this uptime (progress-bar denominator)
    bool    remaining_known = true;   // false: a realtime source says "up" but no duration is known yet
    int64_t uptime_ms = 0;            // how long the boon has been continuously active (0 when inactive)
};

class BoonTracker {
public:
    explicit BoonTracker(Stacking stacking = Stacking::Intensity, int max_stacks = 25)
        : stacking_(stacking), max_stacks_(max_stacks) {}

    // id is arcdps' trackable buff instance id; 0 means "unknown".
    void apply(uint32_t id, int64_t duration_ms, uint64_t now_ms);
    // Stack duration changed (extension/reduction). new_duration_ms wins when > 0,
    // otherwise delta_ms is added to the existing stack.
    void change(uint32_t id, int64_t delta_ms, int64_t new_duration_ms, uint64_t now_ms);
    // Overwrite a known stack's duration; ignored for unknown ids. Returns whether it matched.
    bool set_duration(uint32_t id, int64_t duration_ms, uint64_t now_ms);
    // removed_ms is a hint used to pick a stack when the id is unknown.
    void remove_single(uint32_t id, int64_t removed_ms, uint64_t now_ms);
    void remove_all();

    BoonSnapshot snapshot(uint64_t now_ms);

private:
    struct Stack { uint32_t id; int64_t remaining_ms; };

    void advance(uint64_t now_ms);
    Stack* find(uint32_t id);
    int64_t total_remaining() const;

    Stacking stacking_;
    int max_stacks_;
    std::vector<Stack> stacks_;
    uint64_t last_ms_ = 0;
    int64_t peak_ms_ = 0;
};

}  // namespace core
