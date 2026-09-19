// arcdps extension entry points: get_init_addr / get_release_addr (ImGui 1.92.7).
// When Nexus has loaded the DLL as well, Nexus owns the overlay and options and
// this side only contributes arcdps' direct combat callback.
#include <atomic>
#include <string>
#include "host.hpp"
#include "icon_texture.hpp"
#include "imgui192.hpp"
#include "overlay.hpp"
#include "realtime_source.hpp"
#include "state.hpp"

using namespace plugin;

static arcdps_exports g_arc{};
static std::atomic<bool> g_arc_loaded{false};
static void* g_imgui_ctx = nullptr;
static void* g_malloc = nullptr;
static void* g_free = nullptr;
static void* g_swapchain = nullptr;

namespace plugin {
bool arcdps_is_loaded() { return g_arc_loaded.load(); }
}

static uintptr_t arc_combat(cbtevent* ev, ag* src, ag* dst, const char* /*skillname*/,
                            uint64_t /*id*/, uint64_t /*revision*/) {
    on_combat(Feed::ArcdpsDirect, Channel::Area, ev, src, dst);
    return 0;
}

static uintptr_t arc_combat_local(cbtevent* ev, ag* src, ag* dst, const char* /*skillname*/,
                                  uint64_t /*id*/, uint64_t /*revision*/) {
    on_combat(Feed::ArcdpsDirect, Channel::Local, ev, src, dst);
    return 0;
}

// Nexus has priority for drawing whenever it has loaded us (it can be hot-loaded later).
static uintptr_t arc_imgui(uint32_t not_charsel_or_loading, uint32_t /*hide_if_combat_or_ooc*/) {
    if (!nexus_is_loaded()) {
        realtime_source_poll();   // hooks need Nexus, but a polling source works here too
        ui192::draw_overlay(not_charsel_or_loading != 0, nullptr);
    }
    return 0;
}

static uintptr_t arc_options() {
    if (nexus_is_loaded())
        arc192::ImGui::TextDisabled("Managed by Nexus: see the Nexus options for Boon Magnifier.");
    else
        ui192::draw_options();
    return 0;
}

static arcdps_exports* mod_init() {
    g_arc = {};
    g_arc.size = sizeof(arcdps_exports);
    g_arc.sig = 0xB0041122;
    g_arc.imguivers = IMGUI_VERSION_NUM;   // 1.92.7's, from imgui192.hpp; must equal arcdps' own
    g_arc.out_name = "boon magnifier";
    g_arc.out_build = PLUGIN_VERSION;
    g_arc.combat = (void*)arc_combat;
    g_arc.combat_local = (void*)arc_combat_local;
    // With Nexus already managing us, skip the UI callbacks so arcdps doesn't show
    // a pointless options tab; the settings live in the Nexus options then.
    if (!nexus_is_loaded()) {
        arc192::ImGui::SetCurrentContext((arc192::ImGuiContext*)g_imgui_ctx);
        arc192::ImGui::SetAllocatorFunctions((void* (*)(size_t, void*))g_malloc,
                                             (void (*)(void*, void*))g_free);
        g_arc.imgui = (void*)arc_imgui;
        g_arc.options_end = (void*)arc_options;
    }
    ensure_started(g_swapchain);
    if (!nexus_is_loaded()) realtime_source_init(nullptr);
    g_arc_loaded = true;
    return &g_arc;
}

static uintptr_t mod_release() {
    g_arc_loaded = false;
    config_flush();
    if (!nexus_is_loaded()) { realtime_source_shutdown(); icon_release(); }
    return 0;
}

extern "C" __declspec(dllexport)
void* get_init_addr(char* /*arcversion*/, void* imguictx, void* id3dptr, HANDLE /*arcdll*/,
                    void* mallocfn, void* freefn, uint32_t d3dversion) {
    g_imgui_ctx = imguictx;
    g_malloc = mallocfn;
    g_free = freefn;
    g_swapchain = d3dversion == 11 ? id3dptr : nullptr;
    return (void*)mod_init;
}

// Debug/tooling aid (preview_host, scripts): current realtime-source status line.
extern "C" __declspec(dllexport)
const char* boon_magnifier_status() {
    static std::string s;
    s = realtime_source_status();
    return s.c_str();
}

extern "C" __declspec(dllexport)
void* get_release_addr() {
    return (void*)mod_release;
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = hinst;
        DisableThreadLibraryCalls(hinst);
    }
    return TRUE;
}
