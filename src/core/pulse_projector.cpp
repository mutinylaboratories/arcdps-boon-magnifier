#include "pulse_projector.hpp"
#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace core {

std::vector<PulseSource> PulseProjector::default_sources() {
    return {
        // Hallowed Ground (skill 9253): 8 pulses 1 s apart (measured), ~3 s stability per pulse before boon duration.
        {"Hallowed Ground", 1000, 250, 2000, 6000, 8, 9253, 8000},
    };
}

std::vector<PulseSource> PulseProjector::parse_sources(const std::string& text) {
    std::vector<PulseSource> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        size_t hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        PulseSource s;
        s.name = line.substr(0, eq);
        while (!s.name.empty() && (s.name.back() == ' ' || s.name.back() == '\t')) s.name.pop_back();
        size_t start = s.name.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        s.name.erase(0, start);
        int64_t v[7] = {};
        std::string rest = line.substr(eq + 1);
        std::istringstream parts(rest);
        std::string tok;
        int n = 0;
        while (n < 7 && std::getline(parts, tok, ',')) {
            char* end = nullptr;
            v[n] = std::strtoll(tok.c_str(), &end, 10);
            if (end == tok.c_str()) break;
            ++n;
        }
        if ((n != 5 && n != 7) || v[0] <= 0 || v[4] < 2 || v[2] > v[3]) continue;
        s.interval_ms = v[0];
        s.interval_tol_ms = v[1];
        s.stack_min_ms = v[2];
        s.stack_max_ms = v[3];
        s.pulses = (int)v[4];
        if (n == 7) { s.skill_id = (uint32_t)v[5]; s.field_ms = v[6]; }
        out.push_back(s);
    }
    return out;
}

const PulseSource* PulseProjector::match(int64_t interval_ms, int64_t duration_ms) const {
    for (const PulseSource& s : sources_)
        if (std::llabs(interval_ms - s.interval_ms) <= s.interval_tol_ms &&
            duration_ms >= s.stack_min_ms && duration_ms <= s.stack_max_ms)
            return &s;
    return nullptr;
}

const PulseSource* PulseProjector::match_armed(int64_t duration_ms, const std::vector<uint32_t>& armed) const {
    for (const PulseSource& s : sources_)
        if (s.skill_id && std::find(armed.begin(), armed.end(), s.skill_id) != armed.end() &&
            duration_ms >= s.stack_min_ms && duration_ms <= s.stack_max_ms)
            return &s;
    return nullptr;
}

Projection PulseProjector::update(const std::vector<StackObservation>& stacks, uint64_t now_ms,
                                  const std::vector<uint32_t>& armed_skill_ids) {
    // New stack instances since last call, oldest first.
    std::vector<const StackObservation*> fresh;
    for (const StackObservation& s : stacks)
        if (std::find(known_ids_.begin(), known_ids_.end(), s.id) == known_ids_.end()) fresh.push_back(&s);
    std::sort(fresh.begin(), fresh.end(),
              [](const StackObservation* a, const StackObservation* b) { return a->first_seen_ms < b->first_seen_ms; });
    known_ids_.clear();
    for (const StackObservation& s : stacks) known_ids_.push_back(s.id);

    for (const StackObservation* s : fresh) {
        const bool same_duration = std::llabs(s->duration_ms - train_.duration_ms) <= 150;
        const int64_t gap = train_.count ? (int64_t)(s->first_seen_ms - train_.last_ms) : 0;
        const PulseSource* src = train_.count && same_duration ? match(gap, s->duration_ms) : nullptr;
        if (src && (train_.source == nullptr || train_.source == src)) {
            ++train_.count;
            train_.last_ms = s->first_seen_ms;
            train_.last_id = s->id;
            train_.source = src;
        } else {
            // Not a continuation: this stack may start a new train. If the player has a
            // matching field skill ready, assume it was just cast (provisional projection).
            train_ = Train{s->id, s->duration_ms, s->first_seen_ms, s->first_seen_ms, 1,
                           match_armed(s->duration_ms, armed_skill_ids)};
        }
    }

    Projection p;
    if (!train_.source) return p;
    if (stacks.empty()) { train_ = Train{}; return p; }          // stripped / expired: forget it

    const PulseSource& src = *train_.source;
    // Pulses still expected? Then the next one must arrive within one interval (+tolerance),
    // otherwise the source stopped early (walked out of the field) and the projection ends.
    if (train_.count < src.pulses &&
        (int64_t)(now_ms - train_.last_ms) > src.interval_ms + src.interval_tol_ms) {
        train_.source = nullptr;
        return p;
    }
    p.active = true;
    p.pulses_seen = train_.count;
    p.source = &src;
    p.end_ms = train_.first_ms + (uint64_t)(src.pulses - 1) * (uint64_t)src.interval_ms + (uint64_t)train_.duration_ms;
    return p;
}

}  // namespace core
