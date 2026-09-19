#include "state.hpp"
#include <mmsystem.h>   // timeGetTime (excluded by WIN32_LEAN_AND_MEAN)
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include "icon_texture.hpp"

namespace plugin {

HMODULE g_self = nullptr;
core::Config g_cfg;

namespace {

// Per-boon tracking: both arcdps channels plus the realtime source's last report.
struct BoonSlot {
    uint32_t id;
    struct Ch {
        core::BoonTracker tracker;
        core::BoonEventRouter router;
        Ch(uint32_t id, core::Stacking stacking, int max_stacks) : tracker(stacking, max_stacks), router(tracker, id) {}
    } ch[2];
    core::RealtimeState rt;
    uint64_t active_since = 0;   // continuous uptime, e.g. for a pulsing field like Hallowed Ground

    BoonSlot(uint32_t id_, core::Stacking stacking, int max_stacks)
        : id(id_), ch{Ch(id_, stacking, max_stacks), Ch(id_, stacking, max_stacks)} {}
};

std::mutex g_mtx;
std::vector<std::unique_ptr<BoonSlot>> g_boons;   // guarded by g_mtx
LatencyStats g_lat[2];                             // guarded by g_mtx
unsigned long long g_lat_sum[2] = {0, 0};
std::string g_cfg_path;
bool g_cfg_dirty = false;
bool g_started = false;
std::atomic<Feed> g_feed{Feed::None};
FILE* g_log = nullptr;                             // guarded by g_mtx
unsigned g_rt_stale_ms = 0;                        // guarded by g_mtx
std::vector<SquadMember> g_roster;                  // guarded by g_mtx
std::vector<SquadBoon> g_squad;                     // guarded by g_mtx

BoonSlot* find_slot_locked(uint32_t id) {
    for (auto& b : g_boons)
        if (b->id == id) return b.get();
    return nullptr;
}

void set_tracked_boons_locked(const std::vector<uint32_t>& ids) {
    std::vector<std::unique_ptr<BoonSlot>> next;
    for (uint32_t id : ids) {
        bool kept = false;
        for (auto& b : g_boons)
            if (b && b->id == id) { next.push_back(std::move(b)); kept = true; break; }
        if (kept) continue;
        const core::BoonDef* def = core::find_boon(id);
        next.push_back(std::make_unique<BoonSlot>(id, def ? def->stacking : core::Stacking::Intensity,
                                                  def ? def->max_stacks : 25));
    }
    g_boons = std::move(next);
}

std::string ini_path_next_to_dll() {
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(g_self, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    std::string path(buf, n);
    size_t slash = path.find_last_of("\\/");
    path.resize(slash == std::string::npos ? 0 : slash + 1);
    return path + "arcdps_boon_magnifier.ini";
}

bool is_tracked_buff_event_locked(const cbtevent* ev) {
    if (find_slot_locked(ev->skillid)) return true;
    return ev->is_statechange == CBTS_BUFFACTIVE || ev->is_statechange == CBTS_BUFFDEACTIVE;
}

// Own skill activations, logged to learn whether the local feed delivers them promptly.
bool is_self_activation(const cbtevent* ev, const ag* src) {
    if (!src || !src->self) return false;
    if (ev->is_statechange == CBTS_ANIMATIONSTART || ev->is_statechange == CBTS_ANIMATIONSTOP) return true;
    return ev->is_statechange == CBTS_NONE && ev->is_activation != 0;
}

}  // namespace

void ensure_started(void* swapchain) {
    if (!g_started) {
        g_started = true;
        g_cfg_path = ini_path_next_to_dll();
        core::load_config(g_cfg, g_cfg_path.c_str());
        std::lock_guard<std::mutex> lock(g_mtx);
        set_tracked_boons_locked(g_cfg.boons);
    }
    icon_init(swapchain);   // no-op once the texture exists
}

void set_tracked_boons(const std::vector<uint32_t>& ids) {
    std::lock_guard<std::mutex> lock(g_mtx);
    set_tracked_boons_locked(ids);
}

Feed last_feed() { return g_feed.load(); }

void on_combat(Feed feed, Channel ch, const cbtevent* ev, const ag* src, const ag* dst) {
    g_feed.store(feed);
    // cbtevent.time is timeGetTime() when the game event happened; the difference to
    // now is how late arcdps delivered it, which is subtracted from applied durations.
    const DWORD now = timeGetTime();
    DWORD delay = ev ? now - (DWORD)ev->time : 0;
    int64_t age = delay < 5000 ? delay : 0;

    std::lock_guard<std::mutex> lock(g_mtx);
    if (!ev && src) {
        // Agent notification. src->prof != 0: added (dst carries the details); else removed.
        if (src->prof && dst) {
            SquadMember m{(uint32_t)dst->id, src->id, src->name ? src->name : "", dst->team, dst->self != 0};
            bool found = false;
            for (SquadMember& r : g_roster)
                if (r.arc_id == src->id) { r = m; found = true; }
            if (!found) g_roster.push_back(m);
        } else if (!src->prof) {
            for (size_t i = 0; i < g_roster.size(); ++i)
                if (g_roster[i].arc_id == src->id) { g_roster.erase(g_roster.begin() + i); break; }
        }
    }
    const bool tracked = ev && (is_tracked_buff_event_locked(ev) || is_self_activation(ev, src));
    if (tracked && find_slot_locked(ev->skillid)) {
        LatencyStats& l = g_lat[(int)ch];
        l.last = delay;
        if (delay > l.max) l.max = delay;
        g_lat_sum[(int)ch] += delay;
        l.avg = (unsigned)(g_lat_sum[(int)ch] / ++l.count);
    }
    const uint64_t tick = GetTickCount64();
    for (auto& b : g_boons) b->ch[(int)ch].router.handle(ev, src, dst, tick, age);
    if (g_log && tracked) {
        uint32_t tid;
        std::memcpy(&tid, &ev->pad61, 4);
        BoonSlot* slot = find_slot_locked(ev->skillid);
        core::BoonSnapshot s = slot ? slot->ch[(int)ch].tracker.snapshot(tick) : core::BoonSnapshot{};
        std::fprintf(g_log, "%c,%lu,%llu,%lu,%u,%u,%d,%u,%u,%llu,%llu,%u,%u,%d,%lld,%u,%u\n",
                     ch == Channel::Area ? 'A' : 'L', (unsigned long)now, (unsigned long long)ev->time, (unsigned long)delay,
                     ev->is_statechange, ev->is_buffremove, ev->value, ev->overstack_value, tid,
                     (unsigned long long)ev->src_agent, (unsigned long long)ev->dst_agent,
                     src ? src->self : 9u, dst ? dst->self : 9u, s.stacks, (long long)s.remaining_ms,
                     ev->skillid, ev->is_activation);
        std::fflush(g_log);
    }
}

LatencyStats latency_stats(Channel ch) {
    std::lock_guard<std::mutex> lock(g_mtx);
    return g_lat[(int)ch];
}

void latency_reset() {
    std::lock_guard<std::mutex> lock(g_mtx);
    for (int i = 0; i < 2; ++i) { g_lat[i] = {}; g_lat_sum[i] = 0; }
}

bool event_log_enabled() {
    std::lock_guard<std::mutex> lock(g_mtx);
    return g_log != nullptr;
}

void event_log_enable(bool on) {
    std::lock_guard<std::mutex> lock(g_mtx);
    if (on && !g_log) {
        std::string path = ini_path_next_to_dll();
        path.replace(path.size() - 4, 4, "_events.log");
        g_log = std::fopen(path.c_str(), "a");
        if (g_log)
            std::fputs("channel,recv_ms,ev_time,delay_ms,statechange,is_buffremove,value,overstack,trackable_id,"
                       "src_agent,dst_agent,src_self,dst_self,stacks_after,remaining_after_ms,skillid,is_activation\n", g_log);
    } else if (!on && g_log) {
        std::fclose(g_log);
        g_log = nullptr;
    }
}

core::BoonSnapshot boon_snapshot(uint32_t boon_id, Channel ch) {
    std::lock_guard<std::mutex> lock(g_mtx);
    BoonSlot* b = find_slot_locked(boon_id);
    return b ? b->ch[(int)ch].tracker.snapshot(GetTickCount64()) : core::BoonSnapshot{};
}

core::BoonSnapshot boon_snapshot(uint32_t boon_id) {
    std::lock_guard<std::mutex> lock(g_mtx);
    BoonSlot* b = find_slot_locked(boon_id);
    if (!b) return {};
    const uint64_t now = GetTickCount64();
    core::BoonSnapshot s = core::merge_realtime(b->ch[(int)Channel::Area].tracker.snapshot(now), b->rt, now, g_rt_stale_ms);
    if (!s.active) b->active_since = 0;
    else if (!b->active_since) b->active_since = now;
    s.uptime_ms = s.active ? (int64_t)(now - b->active_since) : 0;
    return s;
}

void realtime_report(uint32_t boon_id, bool active, int stacks, int64_t remaining_ms) {
    std::lock_guard<std::mutex> lock(g_mtx);
    BoonSlot* b = find_slot_locked(boon_id);
    if (!b) return;
    b->rt.valid = true;
    b->rt.active = active;
    b->rt.stacks = stacks;
    b->rt.remaining_ms = remaining_ms;
    b->rt.updated_ms = GetTickCount64();
}

void realtime_disconnect() {
    std::lock_guard<std::mutex> lock(g_mtx);
    for (auto& b : g_boons) b->rt = {};
}

void realtime_set_stale_after(unsigned ms) {
    std::lock_guard<std::mutex> lock(g_mtx);
    g_rt_stale_ms = ms;
}

core::RealtimeState realtime_state(uint32_t boon_id) {
    std::lock_guard<std::mutex> lock(g_mtx);
    BoonSlot* b = find_slot_locked(boon_id);
    return b ? b->rt : core::RealtimeState{};
}

std::vector<SquadMember> squad_roster() {
    std::lock_guard<std::mutex> lock(g_mtx);
    return g_roster;
}

void squad_report(const std::vector<SquadBoon>& state) {
    std::lock_guard<std::mutex> lock(g_mtx);
    g_squad = state;
}

std::vector<SquadBoon> squad_state() {
    std::lock_guard<std::mutex> lock(g_mtx);
    return g_squad;
}

void config_mark_dirty() { g_cfg_dirty = true; }

void config_flush() {
    if (!g_cfg_dirty || g_cfg_path.empty()) return;
    core::save_config(g_cfg, g_cfg_path.c_str());
    g_cfg_dirty = false;
}

}  // namespace plugin
