#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Projects the end of a *pulsing* stability source (Hallowed Ground and friends).
//
// A field applies a fresh, identical-duration stack every pulse; the buff bar only
// ever shows the stacks that exist right now, so the plain countdown hovers at one
// pulse's duration and never reflects that the field will keep going. When two or
// more stacks with the same duration arrive at a steady interval, that pattern is
// matched against a table of known sources and the countdown is extended to
//     first pulse + (pulses - 1) * interval + stack duration
// The projection is dropped the moment a pulse fails to arrive (you left the field)
// or the stacks disappear (strip / CC), so it can only ever be optimistic while the
// pattern is actually continuing.
namespace core {

struct PulseSource {
    std::string name;
    int64_t interval_ms;        // time between pulses
    int64_t interval_tol_ms;    // accepted jitter around interval_ms
    int64_t stack_min_ms;       // per-pulse stack duration range (boon duration stats scale it)
    int64_t stack_max_ms;
    int     pulses;             // total pulses the source delivers
    // Optional, for instant projection from the caster's own skill bar (see realtime_gw2.cpp):
    uint32_t skill_id = 0;      // the skill whose recharge starting marks the cast (0 = pattern only)
    int64_t  field_ms = 0;      // how long the field keeps pulsing after the cast
};

struct StackObservation {       // one stability stack instance as the poll sees it
    uint32_t id;
    int64_t  duration_ms;
    uint64_t first_seen_ms;
};

struct Projection {
    bool     active = false;
    int64_t  end_ms = 0;        // absolute time the last pulse's stack should expire
    int      pulses_seen = 0;
    const PulseSource* source = nullptr;
};

class PulseProjector {
public:
    explicit PulseProjector(std::vector<PulseSource> sources) : sources_(std::move(sources)) {}

    // Feed every frame with the current stacks; returns the projection valid at now_ms.
    // armed_skill_ids: sources the local player could have just cast (skill on the bar and
    // not recharging). For those, the very first matching stack already starts a
    // provisional projection instead of waiting for the second pulse.
    Projection update(const std::vector<StackObservation>& stacks, uint64_t now_ms,
                      const std::vector<uint32_t>& armed_skill_ids = {});

    const std::vector<PulseSource>& sources() const { return sources_; }

    static std::vector<PulseSource> default_sources();
    // "name=interval_ms,tolerance_ms,stack_min_ms,stack_max_ms,pulses[,skill_id,field_ms]" per line;
    // '#' comments.
    // Malformed lines are skipped; returns the parsed list (may be empty).
    static std::vector<PulseSource> parse_sources(const std::string& text);

private:
    struct Train {
        uint32_t last_id = 0;
        int64_t  duration_ms = 0;
        uint64_t first_ms = 0;
        uint64_t last_ms = 0;
        int      count = 0;
        const PulseSource* source = nullptr;
    };
    const PulseSource* match(int64_t interval_ms, int64_t duration_ms) const;
    const PulseSource* match_armed(int64_t duration_ms, const std::vector<uint32_t>& armed) const;

    std::vector<PulseSource> sources_;
    std::vector<uint32_t> known_ids_;
    Train train_;
};

}  // namespace core
