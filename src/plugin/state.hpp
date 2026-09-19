#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "arcdps_defs.hpp"
#include "boon_events.hpp"
#include "boon_tracker.hpp"
#include "boons.hpp"
#include "config.hpp"
#include "realtime_merge.hpp"

#ifndef PLUGIN_VERSION
#define PLUGIN_VERSION "0.0.0-dev"
#endif

// State shared by both hosts. Deliberately free of ImGui: Nexus uses ImGui 1.80
// and arcdps 1.92.7, and each entry point talks to its own copy.
namespace plugin {

extern HMODULE g_self;
extern core::Config g_cfg;

// One-time startup shared by both hosts: load settings, create the icon texture.
// swapchain: IDXGISwapChain* (D3D11) or null.
void ensure_started(void* swapchain);

// Where boon data comes from. arcdps' direct callback is preferred; the Nexus
// ArcDPS Integration events are only used when arcdps did not load us itself.
enum class Feed { None, ArcdpsDirect, NexusEvents };
Feed last_feed();

// arcdps offers two combat callbacks, tracked independently for diagnostics:
//   Area  ("combat")       - the only one carrying boon events, but arcdps deliberately
//                            delays it ~2.65s ("intended for statistics, not realtime").
//   Local ("combat_local") - the client's own combat events; measured 2026-09: no boon
//                            applications at all, so it cannot drive the overlay.
enum class Channel { Area = 0, Local = 1 };

// Combat events arrive on arcdps' thread, rendering happens on the game's.
void on_combat(Feed feed, Channel ch, const cbtevent* ev, const ag* src, const ag* dst);

// Every boon in g_cfg.boons is tracked (one overlay each); snapshots are per boon id.
// Call set_tracked_boons after editing g_cfg.boons so trackers follow.
void set_tracked_boons(const std::vector<uint32_t>& ids);
core::BoonSnapshot boon_snapshot(uint32_t boon_id);                // what the overlay shows (arcdps + realtime)
core::BoonSnapshot boon_snapshot(uint32_t boon_id, Channel ch);   // one arcdps channel alone (diagnostics)

// Diagnostics: how late arcdps hands us tracked-boon events (now - cbtevent.time), in ms.
struct LatencyStats { unsigned last = 0, max = 0, avg = 0, count = 0; };
LatencyStats latency_stats(Channel ch);
void latency_reset();
// Appends every tracked-boon event to arcdps_boon_magnifier_events.log beside the DLL.
bool event_log_enabled();
void event_log_enable(bool on);

// ---- Realtime buff source plug-in point ----
// arcdps' boon feed is ~2.65s late. Anything that knows the local player's boon state
// sooner reports it here (any thread) and the overlay follows it immediately, keeping
// arcdps for durations the source doesn't know.
//   stacks: 0 if unknown. remaining_ms: -1 if unknown.
void realtime_report(uint32_t boon_id, bool active, int stacks, int64_t remaining_ms);
void realtime_disconnect();            // source went away; fall back to arcdps alone
// Polling sources must report at least this often or they are treated as gone (0 = event-driven).
void realtime_set_stale_after(unsigned ms);
core::RealtimeState realtime_state(uint32_t boon_id);   // for diagnostics

// ---- Squad roster and squad boon state ----
// arcdps announces squad members through its agent notifications (name, subgroup, and
// the in-map agent id that indexes the client's character array); membership changes
// slowly, so its delay is harmless here. The realtime source fills in each member's
// boon state every poll.
struct SquadMember {
    uint32_t    agent_id;    // in-map agent id (arcdps "instance id")
    uintptr_t   arc_id;      // arcdps' own unique agent id, used for removal notices
    std::string name;        // character name
    uint16_t    subgroup;    // 0 = not in the squad
    bool        self;
};
std::vector<SquadMember> squad_roster();

struct SquadBoon {
    uint32_t    agent_id;
    std::string name;
    uint16_t    subgroup;
    bool        self;
    bool        readable;     // false: character not found / not readable this pass
    int         stacks;
    int64_t     remaining_ms;
    std::string source;       // squad member who supplied the longest stack, if attributable
    uint64_t    updated_ms;
};
void squad_report(const std::vector<SquadBoon>& state);
std::vector<SquadBoon> squad_state();

void config_mark_dirty();
void config_flush();   // writes the ini if dirty

}  // namespace plugin
