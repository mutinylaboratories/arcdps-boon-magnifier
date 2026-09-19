// Raidcore Nexus entry point: GetAddonDef (ImGui 1.80). Once Nexus has loaded the
// addon it owns the overlay and the options menu, even if arcdps loads the DLL too.
// Combat data: arcdps' direct callback when arcdps loaded us as well, otherwise the
// "ArcDPS Integration" Nexus addon, which relays arcdps callbacks as events.
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <string>
#include "Nexus.h"
#include "host.hpp"
#include "icon_texture.hpp"
#include "imgui.h"
#include "overlay.hpp"
#include "realtime_source.hpp"
#include "state.hpp"

using namespace plugin;

// Payload of EV_ARCDPS_COMBATEVENT_* (RaidcoreGG/GW2-Arcdps-Integration).
struct EvCombatData {
    cbtevent* ev;
    ag*       src;
    ag*       dst;
    char*     skillname;
    uint64_t  id;
    uint64_t  revision;
};

static const char* kEvCombatSquad = "EV_ARCDPS_COMBATEVENT_SQUAD_RAW";   // arcdps "combat"
static const char* kEvCombatLocal = "EV_ARCDPS_COMBATEVENT_LOCAL_RAW";   // arcdps "combat_local"

static AddonDefinition_t g_def{};
static AddonAPI_t* g_api = nullptr;
static std::atomic<bool> g_nexus_loaded{false};
static NexusLinkData_t* g_nexus_link = nullptr;

namespace plugin {
bool nexus_is_loaded() { return g_nexus_loaded.load(); }
}

static void nexus_combat_on(Channel ch, void* args) {
    if (arcdps_is_loaded()) return;   // already getting these straight from arcdps
    auto* d = (EvCombatData*)args;
    if (d) on_combat(Feed::NexusEvents, ch, d->ev, d->src, d->dst);
}
static void nexus_combat(void* args) { nexus_combat_on(Channel::Area, args); }
static void nexus_combat_local(void* args) { nexus_combat_on(Channel::Local, args); }

// Mirror the realtime source's status into Nexus.log whenever it changes, so a session
// can be diagnosed from the file afterwards (Diagnostics panel shows the same text live).
static void log_realtime_status_changes() {
    static std::string last_poll, last_sig;
    std::string sig = realtime_source_status();
    if (sig != last_sig) { last_sig = sig; g_api->Log(LOGL_INFO, "BoonMagnifier", ("signatures: " + sig).c_str()); }
    std::string poll = realtime_poll_status();
    if (poll != last_poll) { last_poll = poll; g_api->Log(LOGL_INFO, "BoonMagnifier", ("poll: " + poll).c_str()); }
    static std::string last_debug;
    std::string debug = realtime_debug_status();
    if (debug != last_debug) { last_debug = debug; g_api->Log(LOGL_INFO, "BoonMagnifier", ("debug: " + debug).c_str()); }
}

static void nexus_render() {
    realtime_source_poll();
    log_realtime_status_changes();
    const bool gameplay = g_nexus_link ? g_nexus_link->IsGameplay : true;
    ui180::draw_overlay(gameplay, g_nexus_link ? g_nexus_link->FontBig : nullptr);
}

static void nexus_options() {
    ui180::draw_options();
}

// Boon icons: Nexus downloads and caches the official 32x32 icon from the wiki.
static void* nexus_icon_loader(uint32_t boon_id) {
    const core::BoonDef* def = core::find_boon(boon_id);
    if (!def || !g_api) return nullptr;
    char ident[48];
    std::snprintf(ident, sizeof(ident), "BOONMAG_ICON_%u", boon_id);
    Texture_t* t = g_api->Textures_GetOrCreateFromURL(ident, "https://wiki.guildwars2.com", def->icon_path);
    return t ? t->Resource : nullptr;
}

static void addon_load(AddonAPI_t* api) {
    g_api = api;
    ImGui::SetCurrentContext((ImGuiContext*)api->ImguiContext);
    ImGui::SetAllocatorFunctions((void* (*)(size_t, void*))api->ImguiMalloc,
                                 (void (*)(void*, void*))api->ImguiFree);
    g_nexus_link = (NexusLinkData_t*)api->DataLink_Get(DL_NEXUS_LINK);
    ensure_started(api->SwapChain);
    set_icon_loader(nexus_icon_loader);
    g_nexus_loaded = true;

    // EMHStatus is an int enum with MH_OK == 0; HookApi keeps ints so realtime_source.hpp needs no Nexus.h.
    HookApi hooks{reinterpret_cast<decltype(HookApi::create)>(api->MinHook_Create),
                  reinterpret_cast<decltype(HookApi::remove)>(api->MinHook_Remove),
                  reinterpret_cast<decltype(HookApi::enable)>(api->MinHook_Enable),
                  reinterpret_cast<decltype(HookApi::disable)>(api->MinHook_Disable)};
    realtime_source_init(&hooks);

    api->Events_Subscribe(kEvCombatSquad, nexus_combat);
    api->Events_Subscribe(kEvCombatLocal, nexus_combat_local);
    api->GUI_Register(RT_Render, nexus_render);
    api->GUI_Register(RT_OptionsRender, nexus_options);
}

static void addon_unload() {
    if (!g_api) return;
    g_api->GUI_Deregister(nexus_options);
    g_api->GUI_Deregister(nexus_render);
    g_api->Events_Unsubscribe(kEvCombatSquad, nexus_combat);
    g_api->Events_Unsubscribe(kEvCombatLocal, nexus_combat_local);
    g_nexus_loaded = false;   // arcdps draws from here on, if it exported its UI callbacks
    realtime_source_shutdown();   // hooks live in Nexus' MinHook; they must go before we do
    config_flush();
    if (!arcdps_is_loaded()) icon_release();
    set_icon_loader(nullptr);
    g_nexus_link = nullptr;
    g_api = nullptr;
}

extern "C" __declspec(dllexport)
AddonDefinition_t* GetAddonDef() {
    g_def.Signature = 0xB0041122;
    g_def.APIVersion = NEXUS_API_VERSION;
    g_def.Name = "Boon Magnifier";
    g_def.Version = {PLUGIN_VERSION_MAJOR, PLUGIN_VERSION_MINOR, PLUGIN_VERSION_PATCH, 0};
    g_def.Author = "darkredbeard";
    g_def.Description = "Big boon icons with live countdowns (Stability by default). Data from the game client and arcdps.";
    g_def.Load = addon_load;
    g_def.Unload = addon_unload;
    g_def.Flags = AF_None;
    g_def.Provider = UP_GitHub;   // Nexus fetches the newest release's .dll asset when its tag outranks Version
    g_def.UpdateLink = "https://github.com/darkredbeard/arcdps-boon-magnifier";
    return &g_def;
}
