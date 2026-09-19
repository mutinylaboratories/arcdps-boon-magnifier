#include "doctest.h"
#include "pulse_projector.hpp"

using namespace core;

static std::vector<StackObservation> pulses(int n, uint64_t t0, int64_t dur = 3650, int64_t step = 1000) {
    std::vector<StackObservation> v;
    for (int i = 0; i < n; ++i) v.push_back({100u + (uint32_t)i, dur, t0 + (uint64_t)i * step});
    return v;
}

TEST_CASE("a single stack projects nothing; the second matching pulse starts the projection") {
    PulseProjector pp(PulseProjector::default_sources());
    CHECK_FALSE(pp.update(pulses(1, 10000), 10000).active);
    Projection p = pp.update(pulses(2, 10000), 11000);
    REQUIRE(p.active);
    CHECK(p.pulses_seen == 2);
    CHECK(p.source->name == "Hallowed Ground");
    CHECK(p.end_ms == 10000 + 7 * 1000 + 3650);   // first pulse + 7 more pulses + one stack
}

TEST_CASE("projection holds while pulses keep arriving, then the field's last stack runs out") {
    PulseProjector pp(PulseProjector::default_sources());
    std::vector<StackObservation> live;
    uint64_t t = 10000;
    for (int i = 0; i < 8; ++i, t += 1000) {
        live.push_back({200u + (uint32_t)i, 3650, t});
        // stacks older than 3650 ms have expired on the bar
        live.erase(std::remove_if(live.begin(), live.end(),
                                  [t](const StackObservation& s) { return t - s.first_seen_ms >= 3650; }),
                   live.end());
        Projection p = pp.update(live, t);
        if (i >= 1) {
            CHECK(p.active);
            CHECK(p.end_ms == 10000 + 7000 + 3650);
        }
    }
    // after the 8th pulse no more are expected: projection stays until the stacks are gone
    CHECK(pp.update(live, t + 2000).active);
    CHECK_FALSE(pp.update({}, t + 4000).active);
}

TEST_CASE("leaving the field: a missed pulse cancels the projection") {
    PulseProjector pp(PulseProjector::default_sources());
    std::vector<StackObservation> live = pulses(3, 10000);
    CHECK(pp.update(live, 12000).active);
    CHECK(pp.update(live, 13200).active);        // within tolerance
    CHECK_FALSE(pp.update(live, 13400).active);  // >1250 ms since the last pulse
}

TEST_CASE("irregular or differently sized stacks do not form a train") {
    PulseProjector pp(PulseProjector::default_sources());
    std::vector<StackObservation> v = {{1, 10960, 10000}, {2, 10960, 10000}, {3, 10960, 10000}};   // Stand Your Ground
    CHECK_FALSE(pp.update(v, 10000).active);
    v = {{4, 3650, 20000}, {5, 3650, 22500}};   // 2.5 s apart: not a 1 s pulse
    CHECK_FALSE(pp.update(v, 22500).active);
    v = {{6, 3650, 30000}, {7, 6000, 31000}};   // different durations
    CHECK_FALSE(pp.update(v, 31000).active);
}

TEST_CASE("source table parsing") {
    auto v = PulseProjector::parse_sources(
        "# comment\n"
        "Hallowed Ground = 1000,250,2000,6000,10\n"
        "bad line\n"
        "Short = 500,100,1000,2000,1\n"        // pulses < 2 rejected
        "Custom Field=2000, 300, 1500, 9000, 5\n"
        "With Cast = 1000,250,2000,6000,10,9253,10000\n"
        "Six = 1000,250,2000,6000,10,9253\n");   // 6 numbers: rejected
    REQUIRE(v.size() == 3);
    CHECK(v[0].name == "Hallowed Ground");
    CHECK(v[0].skill_id == 0);
    CHECK(v[1].name == "Custom Field");
    CHECK(v[1].interval_ms == 2000);
    CHECK(v[1].pulses == 5);
    CHECK(v[2].skill_id == 9253);
    CHECK(v[2].field_ms == 10000);
}

TEST_CASE("an armed source projects from the very first stack, and is dropped if no pulse follows") {
    PulseProjector pp(PulseProjector::default_sources());
    std::vector<uint32_t> armed = {9253};
    Projection p = pp.update(pulses(1, 10000), 10000, armed);
    REQUIRE(p.active);
    CHECK(p.pulses_seen == 1);
    CHECK(p.end_ms == 10000 + 7 * 1000 + 3650);
    CHECK(pp.update(pulses(1, 10000), 11200, armed).active);         // still waiting for pulse 2
    CHECK_FALSE(pp.update(pulses(1, 10000), 11300, armed).active);   // none came: cancelled

    // Not armed (skill recharging or not on the bar): first stack alone projects nothing.
    PulseProjector pp2(PulseProjector::default_sources());
    CHECK_FALSE(pp2.update(pulses(1, 20000), 20000, {}).active);
    CHECK(pp2.update(pulses(2, 20000), 21000, {}).active);
}
