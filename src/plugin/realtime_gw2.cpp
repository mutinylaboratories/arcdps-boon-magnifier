// ---------------------------------------------------------------------------------
// Realtime boon source: polls the local player's buff bar in the game client for every tracked boon.
//
// Pure memory reads, no hooks, no game code executed; every dereference goes through
// safe_read so a stale pointer degrades to "source unavailable", never a crash.
// Layout knowledge comes from docs/gw2-buff-internals.md (Gw2-64.exe, 2026-09). The one
// address that differs per build - the TLS index of the context table - is resolved
// from arcdps_boon_magnifier_sigs.ini ([contexts_tls_index], made by tools/binja).
// The structure offsets below are assumptions that need re-checking after game patches;
// the sig file's build stamp keeps a stale file from being used on a new exe.
//
// Chain (all reads):
//   TEB->ThreadLocalStoragePointer[tls_index] + 0x10   context table (thread-local: see find_context_table)
//     +0x98                                            ChCliContext
//       +0x98                                          controlled character (valid if bit 4 of char+0x178)
//         +0x40                                        combatant (embedded)
//           +0x90                                      CmbtCliBuffBar*
//             +0x20 capacity, +0x24 count, +0x28 table of 0x18-byte {key, pad, CmbtCliBuff*, occupied}
//               CmbtCliBuff: +0x08 skillId, +0x40 durationMs, +0x44 startTimeMs, +0x4c isActive
// ---------------------------------------------------------------------------------
#include <windows.h>
#include <intrin.h>
#include <mmsystem.h>   // timeGetTime (after windows.h; excluded by WIN32_LEAN_AND_MEAN)
#include <tlhelp32.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <vector>
#include "pulse_projector.hpp"
#include "realtime_source.hpp"
#include "state.hpp"

namespace plugin {

namespace {

constexpr uintptr_t kOffCtxTableFromTlsSlot = 0x10;
constexpr uintptr_t kOffChCliContext        = 0x98;
constexpr uintptr_t kOffControlledCharacter = 0x98;
constexpr uintptr_t kOffCharacterFlags      = 0x178;   // bit 4 must be set for GetControlledCharacter() to return it
constexpr uintptr_t kOffCombatant           = 0x40;
constexpr uintptr_t kOffBuffBar             = 0x90;
constexpr uintptr_t kOffBuffsCapacity       = 0x20;
constexpr uintptr_t kOffBuffsCount          = 0x24;
constexpr uintptr_t kOffBuffsTable          = 0x28;
constexpr uintptr_t kBuffMapEntrySize       = 0x18;
constexpr uintptr_t kOffEntryBuff           = 0x08;
constexpr uintptr_t kOffBuffSkillId         = 0x08;
constexpr uintptr_t kOffBuffInstanceId      = 0x18;
constexpr uintptr_t kOffBuffDurationMs      = 0x40;
constexpr uintptr_t kOffBuffStartTimeMs     = 0x44;
constexpr uintptr_t kOffBuffIsActive        = 0x4c;
constexpr uint32_t  kMaxBuffTableSize       = 4096;    // sanity bound on a corrupt/foreign pointer
// Skill bar (ChCliSkillbar / CharSkillbar recharge manager), see docs/gw2-buff-internals.md.
constexpr uintptr_t kOffCharSkillbar         = 0x520;   // ChCliCharacter -> ChCliSkillbar*
constexpr uintptr_t kOffSkillbarRecharge     = 0x150;   // ChCliSkillbar -> CharSkillbar recharge manager* (pointer)
constexpr uintptr_t kOffRechargeBaseTime     = 0x24;    // u32 game time the entries' remaining values refer to
constexpr uintptr_t kOffRechargeRate         = 0x30;    // float, recharge speed multiplier (alacrity)
constexpr uintptr_t kOffRechargeListHead     = 0x48;    // intrusive list; low bit set = end
constexpr uintptr_t kOffRechargeEntryNext    = 0x08;
constexpr uintptr_t kOffRechargeEntryLeft    = 0x10;    // int32 remaining ms at base time, -1 = none
constexpr uintptr_t kOffRechargeEntrySkill   = 0x18;    // skillDef*
constexpr uintptr_t kOffRechargeEntryKind    = 0x24;    // 0 = skill recharge
constexpr uintptr_t kOffSkillDefId           = 0x28;
constexpr uint32_t  kMaxRechargeEntries      = 128;
constexpr uintptr_t kTebTlsPointerOffset    = 0x58;    // TEB.ThreadLocalStoragePointer (x64)
// Squad: ChCliContext's character array is indexed by in-map agent id.
constexpr uintptr_t kOffCharacterArray       = 0x60;    // ChCliContext -> ChCliCharacter*[]
constexpr uintptr_t kOffCharacterArrayCount  = 0x6c;    // u32
constexpr uintptr_t kOffCharacterAgent       = 0x90;    // ChCliCharacter -> agent* (vtable slot 0 getter)
constexpr uintptr_t kOffBuffSourceAgent      = 0x28;    // CmbtCliBuff -> agent* that applied it
constexpr uint32_t  kMaxCharacters           = 65536;

// ---- Locating the context table -------------------------------------------------
// The game keeps its context table in a TLS slot, so only the game's own thread(s)
// have it; the render thread this poll runs on may not. Look at every thread's TEB
// (via NtQueryInformationThread) for a slot whose table has a ChCliContext, then
// cache that table pointer. Re-scan (rate limited) whenever the chain stops reading.

struct ThreadBasicInfo {
    LONG  exit_status;
    void* teb_base;
    struct { void* process; void* thread; } client_id;
    ULONG_PTR affinity_mask;
    LONG  priority;
    LONG  base_priority;
};
using NtQueryInformationThread_t = LONG(__stdcall*)(HANDLE, int, void*, ULONG, ULONG*);

uintptr_t g_ctx_table = 0;        // cached, 0 = unknown
DWORD     g_ctx_thread = 0;       // thread the cached table came from (diagnostic)
ULONGLONG g_next_scan_ms = 0;

bool table_looks_live(uintptr_t table) {
    uintptr_t chctx = 0;
    return table && safe_read(table + kOffChCliContext, chctx) && chctx != 0;
}

uintptr_t table_from_tls_pointer(uintptr_t tlsp, uint32_t index) {
    uintptr_t slot = 0, table = 0;
    if (!tlsp || !safe_read(tlsp + (uintptr_t)index * 8, slot) || !slot) return 0;
    if (!safe_read(slot + kOffCtxTableFromTlsSlot, table)) return 0;
    return table;
}

uintptr_t scan_threads_for_table(uint32_t index) {
    static NtQueryInformationThread_t query = nullptr;
    if (!query) {
        HMODULE ntdll = GetModuleHandleA("ntdll.dll");
        if (ntdll) query = (NtQueryInformationThread_t)GetProcAddress(ntdll, "NtQueryInformationThread");
    }
    if (!query) return 0;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    const DWORD pid = GetCurrentProcessId();
    uintptr_t found = 0;
    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    for (BOOL ok = Thread32First(snap, &te); ok && !found; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid) continue;
        HANDLE h = OpenThread(THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
        if (!h) continue;
        ThreadBasicInfo tbi{};
        if (query(h, 0 /*ThreadBasicInformation*/, &tbi, sizeof(tbi), nullptr) == 0 && tbi.teb_base) {
            uintptr_t tlsp = 0;
            if (safe_read((uintptr_t)tbi.teb_base + kTebTlsPointerOffset, tlsp)) {
                uintptr_t table = table_from_tls_pointer(tlsp, index);
                if (table_looks_live(table)) { found = table; g_ctx_thread = te.th32ThreadID; }
            }
        }
        CloseHandle(h);
    }
    CloseHandle(snap);
    return found;
}

// Returns the cached context table, refreshing it from this thread or a thread scan.
uintptr_t find_context_table() {
    const uintptr_t tls_index_addr = realtime_resolved("contexts_tls_index");
    uint32_t index = 0;
    if (!tls_index_addr || !safe_read(tls_index_addr, index) || index > 1024) {
        realtime_set_poll_status("TLS index unreadable");
        return 0;
    }
    if (table_looks_live(g_ctx_table)) return g_ctx_table;

    g_ctx_table = 0;
    // Cheapest first: our own thread (works if the host renders on the game thread).
    uintptr_t table = table_from_tls_pointer(__readgsqword(kTebTlsPointerOffset), index);
    if (table_looks_live(table)) {
        g_ctx_table = table;
        g_ctx_thread = GetCurrentThreadId();
        return table;
    }
    const ULONGLONG now = GetTickCount64();
    if (now < g_next_scan_ms) { realtime_set_poll_status("context table not found on any thread (rescan pending)"); return 0; }
    g_next_scan_ms = now + 1000;
    g_ctx_table = scan_threads_for_table(index);
    if (!g_ctx_table) realtime_set_poll_status("context table not found on any thread");
    return g_ctx_table;
}

// ---- Pulsing-source table ---------------------------------------------------------
// arcdps_boon_magnifier_pulses.ini beside the DLL overrides the built-in list; format:
//   Name = interval_ms, tolerance_ms, stack_min_ms, stack_max_ms, pulses

std::string pulses_ini_path() {
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(g_self, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    std::string path(buf, n);
    size_t slash = path.find_last_of("\\/");
    path.resize(slash == std::string::npos ? 0 : slash + 1);
    return path + "arcdps_boon_magnifier_pulses.ini";
}

core::PulseProjector& pulse_projector() {
    static core::PulseProjector* instance = nullptr;
    if (!instance) {
        std::vector<core::PulseSource> sources;
        std::ifstream f(pulses_ini_path(), std::ios::binary);
        if (f) {
            std::stringstream ss;
            ss << f.rdbuf();
            sources = core::PulseProjector::parse_sources(ss.str());
        }
        if (sources.empty()) sources = core::PulseProjector::default_sources();
        instance = new core::PulseProjector(sources);
    }
    return *instance;
}

// ---- The poll ---------------------------------------------------------------------

uintptr_t g_chctx = 0;   // ChCliContext seen by the last successful find_buff_bar

bool find_buff_bar(uintptr_t& out_bar, uintptr_t& out_character) {
    const uintptr_t table = find_context_table();
    if (!table) return false;
    uintptr_t chctx = 0, character = 0, bar = 0;
    uint32_t flags = 0;
    if (!safe_read(table + kOffChCliContext, chctx) || !chctx) { realtime_set_poll_status("no ChCliContext"); return false; }
    if (!safe_read(chctx + kOffControlledCharacter, character) || !character) { realtime_set_poll_status("no controlled character (loading / character select?)"); return false; }
    if (!safe_read(character + kOffCharacterFlags, flags) || !((flags >> 4) & 1)) { realtime_set_poll_status("character present but not flagged controlled"); return false; }
    if (!safe_read(character + kOffCombatant + kOffBuffBar, bar) || !bar) { realtime_set_poll_status("character has no buff bar"); return false; }
    out_bar = bar;
    out_character = character;
    g_chctx = chctx;
    return true;
}

// ---- Squad members' buff bars ----------------------------------------------------------
// Same chain as our own, starting from ChCliContext.m_characterArray[agentId]. Reads are
// batched (one per table, one per buff) and the pass runs at ~10 Hz: ten members with a
// hundred buffs each is a thousand reads otherwise.

bool read_bytes(uintptr_t address, void* out, size_t n) {
    SIZE_T got = 0;
    return address && ReadProcessMemory(GetCurrentProcess(), (LPCVOID)address, out, n, &got) && got == n;
}

struct MemberBoon { int stacks = 0; int64_t remaining_ms = 0; uintptr_t longest_source = 0; };

// Longest-remaining stack of `boon_id` on the character's bar and who applied it.
bool read_member_boon(uintptr_t character, uint32_t boon_id, uint32_t now, MemberBoon& out) {
    uintptr_t bar = 0;
    uint32_t capacity = 0, count = 0;
    uintptr_t entries = 0;
    if (!safe_read(character + kOffCombatant + kOffBuffBar, bar) || !bar) return false;
    if (!safe_read(bar + kOffBuffsCapacity, capacity) || !safe_read(bar + kOffBuffsCount, count) ||
        !safe_read(bar + kOffBuffsTable, entries) || capacity > kMaxBuffTableSize || !entries)
        return false;
    if (count == 0) return true;

    std::vector<uint8_t> table((size_t)capacity * kBuffMapEntrySize);
    if (!read_bytes(entries, table.data(), table.size())) return false;
    for (uint32_t i = 0; i < capacity; ++i) {
        uintptr_t buff = 0;
        std::memcpy(&buff, &table[(size_t)i * kBuffMapEntrySize + kOffEntryBuff], sizeof(buff));
        if (!buff) continue;
        uint8_t raw[0x50];
        if (!read_bytes(buff, raw, sizeof(raw))) return false;
        uint32_t skill, duration, start, active;
        uintptr_t source;
        std::memcpy(&skill, raw + kOffBuffSkillId, 4);
        if (skill != boon_id) continue;
        std::memcpy(&duration, raw + kOffBuffDurationMs, 4);
        std::memcpy(&start, raw + kOffBuffStartTimeMs, 4);
        std::memcpy(&active, raw + kOffBuffIsActive, 4);
        std::memcpy(&source, raw + kOffBuffSourceAgent, sizeof(source));
        ++out.stacks;
        // Without per-instance history for other players, trust the stored start time for the
        // active stack and the full duration for the rest (slightly optimistic on old stacks).
        const int64_t left = active ? std::max<int64_t>(0, (int64_t)(int32_t)(start + duration - now)) : (int64_t)duration;
        if (left >= out.remaining_ms) { out.remaining_ms = left; out.longest_source = source; }
    }
    return true;
}

void poll_squad(uint32_t now) {
    static ULONGLONG next_ms = 0;
    const ULONGLONG tick = GetTickCount64();
    if (tick < next_ms || !g_chctx) return;
    next_ms = tick + 100;

    const std::vector<SquadMember> roster = squad_roster();
    uintptr_t chars = 0;
    uint32_t char_count = 0;
    if (!safe_read(g_chctx + kOffCharacterArray, chars) || !safe_read(g_chctx + kOffCharacterArrayCount, char_count) ||
        !chars || char_count > kMaxCharacters)
        return;

    // Resolve every member's character and agent pointer first, for source attribution.
    struct Resolved { const SquadMember* m; uintptr_t character; uintptr_t agent; };
    std::vector<Resolved> members;
    for (const SquadMember& m : roster) {
        if (!m.subgroup || m.agent_id >= char_count) continue;
        uintptr_t character = 0, agent = 0;
        if (!safe_read(chars + (uintptr_t)m.agent_id * 8, character) || !character) { members.push_back({&m, 0, 0}); continue; }
        safe_read(character + kOffCharacterAgent, agent);
        members.push_back({&m, character, agent});
    }

    const uint32_t boon = g_cfg.boons.empty() ? core::kBuffStability : g_cfg.boons.front();
    std::vector<SquadBoon> out;
    for (const Resolved& r : members) {
        SquadBoon sb{r.m->agent_id, r.m->name, r.m->subgroup, r.m->self, false, 0, 0, "", tick};
        MemberBoon mb;
        if (r.character && read_member_boon(r.character, boon, now, mb)) {
            sb.readable = true;
            sb.stacks = mb.stacks;
            sb.remaining_ms = mb.remaining_ms;
            for (const Resolved& o : members)
                if (o.agent && o.agent == mb.longest_source) sb.source = o.m->name;
        }
        out.push_back(sb);
    }
    squad_report(out);

    // Diagnostic summary (mirrored to Nexus.log when it changes).
    static char line[256];
    int readable = 0, with_boon = 0, attributed = 0;
    for (const SquadBoon& sb : out) { readable += sb.readable; with_boon += sb.stacks > 0; attributed += !sb.source.empty(); }
    std::snprintf(line, sizeof(line), "squad: %zu in roster, %zu in squad, %d readable, %d with boon, %d attributed (char array %u)",
                  roster.size(), out.size(), readable, with_boon, attributed, char_count);
    realtime_set_debug_status(line);
}

// ---- Skill bar: which pulsing-field skills could the player have just cast? -------------
// Reads the 23 slot skill defs and the recharge list. A source is "armed" when its skill
// is on the bar and not recharging; the projector then treats the first matching stack
// as the cast (Hallowed Ground's first pulse lands at cast start, its recharge entry only
// appears at cast end ~1 s later, so the recharge itself is too late to be the trigger).
constexpr uintptr_t kOffSkillbarSlotDefs = 0x1d0;   // ChCliSkillbar -> skillDef*[23]
constexpr uint32_t  kSkillbarSlots       = 23;

std::vector<uint32_t> armed_field_skills(uintptr_t character) {
    std::vector<uint32_t> armed;
    uintptr_t skillbar = 0;
    if (!safe_read(character + kOffCharSkillbar, skillbar) || !skillbar) return armed;

    // Skills on the bar.
    std::vector<uint32_t> on_bar;
    for (uint32_t slot = 0; slot < kSkillbarSlots; ++slot) {
        uintptr_t def = 0;
        uint32_t id = 0;
        if (safe_read(skillbar + kOffSkillbarSlotDefs + slot * 8, def) && def && safe_read(def + kOffSkillDefId, id) && id)
            on_bar.push_back(id);
    }

    // Skills currently recharging (raw stored values; the recharge time base is on a game
    // clock, but presence in the list is all that matters here).
    std::vector<uint32_t> recharging;
    uintptr_t mgr = 0, entry = 0;
    uint32_t base = 0;
    float rate = 1.0f;
    static uint32_t last_count = 0xFFFFFFFF;
    static char dump[512];
    uint32_t count = 0;
    int dump_len = 0;
    if (safe_read(skillbar + kOffSkillbarRecharge, mgr) && mgr &&
        safe_read(mgr + kOffRechargeBaseTime, base) && safe_read(mgr + kOffRechargeRate, rate) &&
        safe_read(mgr + kOffRechargeListHead, entry)) {
        for (uint32_t n = 0; n < kMaxRechargeEntries && entry && !(entry & 1); ++n) {
            uintptr_t skill_def = 0, next = 0;
            int32_t left_at_base = -1;
            uint32_t kind = 0, skill_id = 0;
            if (!safe_read(entry + kOffRechargeEntryNext, next) || !safe_read(entry + kOffRechargeEntrySkill, skill_def) ||
                !safe_read(entry + kOffRechargeEntryLeft, left_at_base) || !safe_read(entry + kOffRechargeEntryKind, kind))
                break;
            ++count;
            if (skill_def) safe_read(skill_def + kOffSkillDefId, skill_id);
            if (kind == 0 && skill_id && left_at_base > 0) recharging.push_back(skill_id);
            if (dump_len < (int)sizeof(dump) - 40)
                dump_len += std::snprintf(dump + dump_len, sizeof(dump) - dump_len, " %u/k%u/%d", skill_id, kind, left_at_base);
            entry = next;
        }
        constexpr bool kDumpRechargeList = false;   // layout confirmed 2026-09-19; flip on to re-check after a patch
        if (kDumpRechargeList && count != last_count) {   // diagnostic, mirrored to Nexus.log
            last_count = count;
            static char line[640];
            std::snprintf(line, sizeof(line), "recharge list: %u entries, base %u, rate %.2f:%s", count, base, rate, dump);
            realtime_set_debug_status(line);
        }
    }

    for (const core::PulseSource& src : pulse_projector().sources()) {
        if (!src.skill_id) continue;
        const bool present = std::find(on_bar.begin(), on_bar.end(), src.skill_id) != on_bar.end();
        const bool cooling = std::find(recharging.begin(), recharging.end(), src.skill_id) != recharging.end();
        if (present && !cooling) armed.push_back(src.skill_id);
    }
    return armed;
}

// Per-boon accumulation for one poll pass.
struct BoonAccum {
    const core::BoonDef* def = nullptr;
    int stacks = 0;
    int64_t intensity_max_ms = 0;   // intensity: longest stack
    int64_t active_left_ms = 0;     // duration-stacked: the ticking stack
    int64_t queued_ms = 0;          // duration-stacked: full durations of the queued stacks
    std::vector<core::StackObservation> observed;
    int64_t remaining() const {
        return def && def->stacking == core::Stacking::Duration ? active_left_ms + queued_ms : intensity_max_ms;
    }
};

void poll_self_boons() {
    uintptr_t bar = 0, character = 0;
    if (!find_buff_bar(bar, character)) { realtime_disconnect(); return; }

    uint32_t capacity = 0, count = 0;
    uintptr_t entries = 0;
    if (!safe_read(bar + kOffBuffsCapacity, capacity) || !safe_read(bar + kOffBuffsCount, count) ||
        !safe_read(bar + kOffBuffsTable, entries) || capacity > kMaxBuffTableSize) {
        realtime_set_poll_status("buff bar unreadable (layout changed?)");
        realtime_disconnect();
        return;
    }

    const uint32_t now = timeGetTime();   // the client's GetTimeMs() is timeGetTime()
    // Each stack instance is timed from when this poll first saw it, on the plugin's own
    // clock; the game's stored start is used for the flagged-active stack (it is exact and
    // covers stacks that predate the plugin). The clock offset is reported for diagnosis.
    struct Seen { uint32_t first_seen_ms; uint32_t duration_ms; uint32_t generation; };
    static std::unordered_map<uint32_t, Seen> seen;
    static uint32_t generation = 0;
    static int64_t clock_offset_ms = 0;
    ++generation;

    std::vector<BoonAccum> acc;
    for (uint32_t id : g_cfg.boons) {
        BoonAccum a;
        a.def = core::find_boon(id);
        acc.push_back(a);
    }
    auto accum_for = [&](uint32_t skill) -> BoonAccum* {
        for (size_t i = 0; i < acc.size(); ++i)
            if (g_cfg.boons[i] == skill) return &acc[i];
        return nullptr;
    };

    int total = 0;
    for (uint32_t i = 0; i < capacity && count > 0 && entries; ++i) {
        uintptr_t buff = 0;
        if (!safe_read(entries + i * kBuffMapEntrySize + kOffEntryBuff, buff)) break;
        if (!buff) continue;
        ++total;
        uint32_t skill = 0;
        if (!safe_read(buff + kOffBuffSkillId, skill)) break;
        BoonAccum* a = accum_for(skill);
        if (!a) continue;

        uint32_t instance = 0, duration = 0, start = 0, active = 0;
        if (!safe_read(buff + kOffBuffInstanceId, instance) || !safe_read(buff + kOffBuffDurationMs, duration) ||
            !safe_read(buff + kOffBuffStartTimeMs, start) || !safe_read(buff + kOffBuffIsActive, active))
            break;
        ++a->stacks;

        auto it = seen.find(instance);
        if (it == seen.end()) {
            it = seen.emplace(instance, Seen{now, duration, generation}).first;
            clock_offset_ms = (int64_t)(int32_t)(now - start);   // ~0 if the game clock is timeGetTime
        } else {
            it->second.generation = generation;
            if (duration != it->second.duration_ms) {            // extension / reduction: restart the timer
                it->second.duration_ms = duration;
                it->second.first_seen_ms = now;
            }
        }
        const uint64_t started = active ? (uint64_t)start : it->second.first_seen_ms;
        const int64_t left = std::max<int64_t>(0, (int64_t)(int32_t)((uint32_t)started + it->second.duration_ms - now));
        if (a->def && a->def->stacking == core::Stacking::Duration) {
            // Duration-stacked boons really do queue: only the flagged-active stack ticks and
            // the others wait with their full duration.
            if (active) a->active_left_ms = std::max(a->active_left_ms, left);
            else a->queued_ms += it->second.duration_ms;
        } else {
            // Intensity: every stack ticks from application; the "active" flag only picks the
            // one the game's UI shows.
            a->intensity_max_ms = std::max(a->intensity_max_ms, left);
        }
        a->observed.push_back({instance, (int64_t)it->second.duration_ms, it->second.first_seen_ms});
    }
    // Forget instances no longer on the bar.
    for (auto it = seen.begin(); it != seen.end();)
        it = (it->second.generation != generation) ? seen.erase(it) : std::next(it);

    // Pulsing sources (Hallowed Ground): extend Stability's countdown to the field's projected
    // end. Own casts are recognised on the first pulse via the skill bar; fields others drop
    // on us from their pulse rhythm after the second stack.
    core::Projection proj;
    for (size_t i = 0; i < acc.size(); ++i) {
        int64_t remaining = acc[i].remaining();
        if (g_cfg.boons[i] == core::kBuffStability) {
            const std::vector<uint32_t> armed = g_cfg.project_pulses ? armed_field_skills(character) : std::vector<uint32_t>{};
            proj = pulse_projector().update(acc[i].observed, now, armed);
            if (proj.active && g_cfg.project_pulses) remaining = std::max(remaining, (int64_t)(proj.end_ms - now));
        }
        realtime_report(g_cfg.boons[i], acc[i].stacks > 0, acc[i].stacks, acc[i].stacks > 0 ? remaining : -1);
    }

    static char status[200];   // no per-frame numbers here: changes are mirrored to Nexus.log
    int len = std::snprintf(status, sizeof(status), "ok: %d buffs on bar;", total);
    for (size_t i = 0; i < acc.size() && len < (int)sizeof(status) - 24; ++i)
        len += std::snprintf(status + len, sizeof(status) - len, " %s %d", acc[i].def ? acc[i].def->abbrev : "?", acc[i].stacks);
    if (proj.active)
        std::snprintf(status + len, sizeof(status) - len, ", projecting %s (%d pulses seen)", proj.source->name.c_str(), proj.pulses_seen);
    else
        std::snprintf(status + len, sizeof(status) - len, " (thread %lu, clock offset %lld ms)", (unsigned long)g_ctx_thread, (long long)clock_offset_ms);
    realtime_set_poll_status(status);
    if (g_cfg.show_squad) poll_squad(now);
}

const Gw2Bindings kBindings{nullptr, 0, poll_self_boons};

}  // namespace

const Gw2Bindings& gw2_bindings() { return kBindings; }

}  // namespace plugin
