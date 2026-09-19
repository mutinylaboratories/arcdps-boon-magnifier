#include "doctest.h"
#include "realtime_merge.hpp"

using namespace core;

static BoonSnapshot arc(bool active, int stacks, int64_t remaining, int64_t peak) {
    BoonSnapshot s;
    s.active = active;
    s.stacks = stacks;
    s.remaining_ms = remaining;
    s.peak_ms = peak;
    return s;
}

TEST_CASE("without a realtime source the arcdps snapshot passes through") {
    BoonSnapshot out = merge_realtime(arc(true, 3, 4000, 8000), RealtimeState{}, 1000, 0);
    CHECK(out.active);
    CHECK(out.stacks == 3);
    CHECK(out.remaining_ms == 4000);
    CHECK(out.remaining_known);
}

TEST_CASE("realtime 'up' before arcdps has caught up: active with unknown duration") {
    RealtimeState rt{true, true, 5, -1, 1000};
    BoonSnapshot out = merge_realtime(arc(false, 0, 0, 0), rt, 1200, 0);
    CHECK(out.active);
    CHECK(out.stacks == 5);
    CHECK_FALSE(out.remaining_known);
}

TEST_CASE("once arcdps delivers, its duration fills in; unknown stack count falls back too") {
    RealtimeState rt{true, true, 0, -1, 1000};
    BoonSnapshot out = merge_realtime(arc(true, 5, 8300, 10960), rt, 3700, 0);
    CHECK(out.active);
    CHECK(out.stacks == 5);
    CHECK(out.remaining_ms == 8300);
    CHECK(out.peak_ms == 10960);
    CHECK(out.remaining_known);
}

TEST_CASE("realtime 'gone' wins immediately over arcdps' lingering stacks (strip / CC)") {
    RealtimeState rt{true, false, 0, -1, 1000};
    BoonSnapshot out = merge_realtime(arc(true, 4, 6000, 8000), rt, 1100, 0);
    CHECK_FALSE(out.active);
    CHECK(out.stacks == 0);
}

TEST_CASE("a source that knows the duration keeps ticking between reports") {
    RealtimeState rt{true, true, 2, 5000, 1000};
    BoonSnapshot out = merge_realtime(arc(false, 0, 0, 0), rt, 2500, 0);
    CHECK(out.remaining_known);
    CHECK(out.remaining_ms == 3500);
    CHECK(merge_realtime(arc(false, 0, 0, 0), rt, 9000, 0).remaining_ms == 0);
}

TEST_CASE("a polling source that went quiet is ignored after the stale timeout") {
    RealtimeState rt{true, true, 1, -1, 1000};
    CHECK(merge_realtime(arc(false, 0, 0, 0), rt, 1400, 500).active);
    CHECK_FALSE(merge_realtime(arc(false, 0, 0, 0), rt, 1600, 500).active);   // falls back to arcdps: inactive
}
