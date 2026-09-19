#include "doctest.h"
#include <cstring>
#include "boon_events.hpp"

using namespace core;

namespace {

struct Fixture {
    BoonTracker tracker;
    BoonEventRouter router{tracker, kBuffStability};
    ag me{"Me", 0x1000, 1, 0, 1, 0};
    ag other{"Other", 0x2000, 2, 0, 0, 0};

    static cbtevent make(uint8_t statechange, uint32_t skill, int32_t value, uint32_t trackable) {
        cbtevent ev{};
        ev.is_statechange = statechange;
        ev.skillid = skill;
        ev.value = value;
        std::memcpy(&ev.pad61, &trackable, 4);
        return ev;
    }
};

}  // namespace

TEST_CASE_FIXTURE(Fixture, "statechange apply on self is tracked, with delivery delay subtracted") {
    cbtevent ev = make(CBTS_BUFFAPPLY, kBuffStability, 5000, 11);
    router.handle(&ev, &other, &me, 1000, 200);
    auto s = tracker.snapshot(1000);
    CHECK(s.stacks == 1);
    CHECK(s.remaining_ms == 4800);
}

TEST_CASE_FIXTURE(Fixture, "other players and other buffs are ignored") {
    cbtevent ev = make(CBTS_BUFFAPPLY, kBuffStability, 5000, 11);
    router.handle(&ev, &me, &other, 1000, 0);
    cbtevent might = make(CBTS_BUFFAPPLY, 740, 5000, 12);
    router.handle(&might, &me, &me, 1000, 0);
    CHECK_FALSE(tracker.snapshot(1000).active);
}

TEST_CASE_FIXTURE(Fixture, "self is recognised by agent id once announced, even without the self flag") {
    ag added{"Me", 0x1000, 1, 0, 0, 0};
    ag info{"acct", 5, 1, 0, 1, 0};
    router.handle(nullptr, &added, &info, 1000, 0);

    ag anon{nullptr, 0x1000, 1, 0, 0, 0};
    cbtevent ev = make(CBTS_BUFFAPPLY, kBuffStability, 5000, 11);
    ev.dst_agent = 0x1000;
    router.handle(&ev, &other, &anon, 1000, 0);
    CHECK(tracker.snapshot(1000).active);
}

TEST_CASE_FIXTURE(Fixture, "statechange change / remove single / remove all") {
    cbtevent a = make(CBTS_BUFFAPPLY, kBuffStability, 5000, 1);
    cbtevent b = make(CBTS_BUFFAPPLY, kBuffStability, 3000, 2);
    router.handle(&a, &me, &me, 1000, 0);
    router.handle(&b, &me, &me, 1000, 0);

    cbtevent ext = make(CBTS_BUFFCHANGE, kBuffStability, 2000, 1);
    ext.overstack_value = 7000;
    router.handle(&ext, nullptr, &me, 1000, 0);
    CHECK(tracker.snapshot(1000).remaining_ms == 7000);

    cbtevent rm = make(CBTS_BUFFREMOVE_SINGLE, kBuffStability, 7000, 1);
    router.handle(&rm, &me, &other, 1000, 0);
    auto s = tracker.snapshot(1000);
    CHECK(s.stacks == 1);
    CHECK(s.remaining_ms == 3000);

    cbtevent all = make(CBTS_BUFFREMOVE_ALL, kBuffStability, 3000, 0);
    router.handle(&all, &me, &other, 1000, 0);
    CHECK_FALSE(tracker.snapshot(1000).active);
}

TEST_CASE_FIXTURE(Fixture, "buff initial seeds existing stacks") {
    cbtevent ev = make(CBTS_BUFFINITIAL, kBuffStability, 2500, 3);
    router.handle(&ev, &me, &me, 1000, 0);
    CHECK(tracker.snapshot(1000).remaining_ms == 2500);
}

TEST_CASE_FIXTURE(Fixture, "legacy encoding: apply, extension, removals") {
    cbtevent a = make(CBTS_NONE, kBuffStability, 4000, 21);
    a.buff = 1;
    router.handle(&a, &other, &me, 1000, 0);
    CHECK(tracker.snapshot(1000).remaining_ms == 4000);

    cbtevent ext = a;
    ext.is_offcycle = 1;
    ext.value = 1000;
    ext.overstack_value = 5000;
    router.handle(&ext, &other, &me, 1000, 0);
    CHECK(tracker.snapshot(1000).remaining_ms == 5000);

    cbtevent rm = make(CBTS_NONE, kBuffStability, 5000, 21);
    rm.buff = 1;
    rm.is_buffremove = CBTB_SINGLE;
    router.handle(&rm, &me, &other, 1000, 0);
    CHECK_FALSE(tracker.snapshot(1000).active);

    router.handle(&a, &other, &me, 1000, 0);
    rm.is_buffremove = CBTB_ALL;
    router.handle(&rm, &me, &other, 1000, 0);
    CHECK_FALSE(tracker.snapshot(1000).active);
}

TEST_CASE_FIXTURE(Fixture, "legacy buff damage ticks and activations are not applications") {
    cbtevent dmg = make(CBTS_NONE, kBuffStability, 0, 0);
    dmg.buff = 1;
    dmg.buff_dmg = 100;
    router.handle(&dmg, &other, &me, 1000, 0);
    cbtevent act = make(CBTS_NONE, kBuffStability, 500, 0);
    act.buff = 1;
    act.is_activation = 1;
    router.handle(&act, &other, &me, 1000, 0);
    CHECK_FALSE(tracker.snapshot(1000).active);
}

TEST_CASE_FIXTURE(Fixture, "map change clears stacks") {
    cbtevent a = make(CBTS_BUFFAPPLY, kBuffStability, 5000, 1);
    router.handle(&a, &me, &me, 1000, 0);
    cbtevent mc = make(CBTS_MAPCHANGE, 0, 0, 0);
    router.handle(&mc, nullptr, nullptr, 1000, 0);
    CHECK_FALSE(tracker.snapshot(1000).active);
}

TEST_CASE_FIXTURE(Fixture, "buff-active events only touch a stack when they name our buff") {
    cbtevent a = make(CBTS_BUFFAPPLY, kBuffStability, 5000, 77);
    router.handle(&a, &me, &me, 1000, 0);

    // Another buff reusing instance id 77 (seen live: periodic skill-less events with value 4000).
    cbtevent other = make(CBTS_BUFFACTIVE, 0, 9000, 0);
    other.dst_agent = 77;
    router.handle(&other, &me, nullptr, 1000, 0);
    CHECK(tracker.snapshot(1000).remaining_ms == 5000);

    cbtevent ours = make(CBTS_BUFFACTIVE, kBuffStability, 4200, 0);
    ours.dst_agent = 77;
    router.handle(&ours, &me, nullptr, 1000, 0);
    CHECK(tracker.snapshot(1000).remaining_ms == 4200);
}
