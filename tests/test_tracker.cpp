#include "doctest.h"
#include "boon_tracker.hpp"

using core::BoonTracker;
using core::Stacking;

TEST_CASE("inactive until something is applied") {
    BoonTracker t;
    auto s = t.snapshot(1000);
    CHECK_FALSE(s.active);
    CHECK(s.stacks == 0);
    CHECK(s.remaining_ms == 0);
}

TEST_CASE("single stack counts down and expires") {
    BoonTracker t;
    t.apply(1, 5000, 1000);
    auto s = t.snapshot(3000);
    CHECK(s.active);
    CHECK(s.stacks == 1);
    CHECK(s.remaining_ms == 3000);
    CHECK(s.peak_ms == 5000);
    CHECK_FALSE(t.snapshot(6000).active);
    CHECK(t.snapshot(6000).peak_ms == 0);
}

TEST_CASE("intensity stacks tick together; remaining is the longest") {
    BoonTracker t;
    t.apply(1, 3000, 1000);
    t.apply(2, 8000, 2000);
    auto s = t.snapshot(3000);
    CHECK(s.stacks == 2);
    CHECK(s.remaining_ms == 7000);
    s = t.snapshot(5000);   // first stack gone at 4000
    CHECK(s.stacks == 1);
    CHECK(s.remaining_ms == 5000);
}

TEST_CASE("re-applying a known id refreshes instead of duplicating") {
    BoonTracker t;
    t.apply(7, 3000, 1000);
    t.apply(7, 6000, 2000);
    auto s = t.snapshot(2000);
    CHECK(s.stacks == 1);
    CHECK(s.remaining_ms == 6000);
}

TEST_CASE("id 0 is treated as unknown and always adds") {
    BoonTracker t;
    t.apply(0, 3000, 1000);
    t.apply(0, 3000, 1000);
    CHECK(t.snapshot(1000).stacks == 2);
}

TEST_CASE("remove_single by id, and by duration hint when the id is unknown") {
    BoonTracker t;
    t.apply(1, 3000, 1000);
    t.apply(2, 8000, 1000);
    t.apply(3, 5000, 1000);
    t.remove_single(2, 0, 1000);
    CHECK(t.snapshot(1000).remaining_ms == 5000);
    t.remove_single(99, 4900, 1000);   // closest to stack 3
    auto s = t.snapshot(1000);
    CHECK(s.stacks == 1);
    CHECK(s.remaining_ms == 3000);
}

TEST_CASE("remove_all clears everything") {
    BoonTracker t;
    t.apply(1, 3000, 1000);
    t.apply(2, 8000, 1000);
    t.remove_all();
    CHECK_FALSE(t.snapshot(1000).active);
}

TEST_CASE("change extends, prefers the absolute duration, and adopts unknown stacks") {
    BoonTracker t;
    t.apply(1, 3000, 1000);
    t.change(1, 2000, 0, 2000);       // delta only: 2000 left + 2000
    CHECK(t.snapshot(2000).remaining_ms == 4000);
    t.change(1, 123, 9000, 2000);     // absolute wins
    CHECK(t.snapshot(2000).remaining_ms == 9000);
    t.change(5, 1000, 4000, 2000);    // never saw the apply
    CHECK(t.snapshot(2000).stacks == 2);
}

TEST_CASE("stack cap replaces the shortest stack") {
    BoonTracker t(Stacking::Intensity, 2);
    t.apply(1, 1000, 1000);
    t.apply(2, 5000, 1000);
    t.apply(3, 3000, 1000);
    auto s = t.snapshot(1000);
    CHECK(s.stacks == 2);
    t.remove_single(1, 0, 1000);      // id 1 is already gone, hint 0 drops the shortest left
    CHECK(t.snapshot(1000).remaining_ms == 5000);
}

TEST_CASE("duration stacking queues stacks") {
    BoonTracker t(Stacking::Duration, 5);
    t.apply(1, 3000, 1000);
    t.apply(2, 4000, 1000);
    CHECK(t.snapshot(1000).remaining_ms == 7000);
    auto s = t.snapshot(5000);        // 4s elapsed: first stack consumed, 1s into the second
    CHECK(s.stacks == 1);
    CHECK(s.remaining_ms == 3000);
    CHECK_FALSE(t.snapshot(8000).active);
}

TEST_CASE("clock going backwards is ignored") {
    BoonTracker t;
    t.apply(1, 5000, 2000);
    CHECK(t.snapshot(1500).remaining_ms == 5000);
    CHECK(t.snapshot(3000).remaining_ms == 4000);
}
