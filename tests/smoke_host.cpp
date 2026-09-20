// Headless stand-in for the hosts (no GPU). Loads the built DLL, feeds stability
// events and renders ImGui frames. Built twice, because the hosts use different
// ImGui versions and this process has to own a matching context:
//   smoke_nexus  (ImGui 1.80)   - Nexus alone, then arcdps loading the DLL on top
//                                 (the real-world dual-load case; Nexus keeps the UI)
//   smoke_arcdps (ImGui 1.92.7, SMOKE_ARCDPS defined) - plain arcdps, no Nexus
// Usage: smoke_xxx <path-to-arcdps_boon_magnifier.dll>
#include <windows.h>
#include <mmsystem.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "Nexus.h"
#include "arcdps_defs.hpp"
#include "imgui.h"

static void* host_malloc(size_t n, void*) { return malloc(n); }
static void host_free(void* p, void*) { free(p); }

// ---- Minimal fake Nexus: records what the addon registers ----
static GUI_RENDER g_nexus_render = nullptr;
static GUI_RENDER g_nexus_options = nullptr;
static EVENT_CONSUME g_nexus_combat = nullptr;
static void fake_gui_register(ERenderType t, GUI_RENDER cb) { (t == RT_Render ? g_nexus_render : g_nexus_options) = cb; }
static void fake_gui_deregister(GUI_RENDER cb) {
    if (g_nexus_render == cb) g_nexus_render = nullptr;
    if (g_nexus_options == cb) g_nexus_options = nullptr;
}
// The overlay is driven by the area ("squad") feed, so that is the channel the tests use.
static void fake_subscribe(const char* id, EVENT_CONSUME cb) { if (std::strstr(id, "SQUAD")) g_nexus_combat = cb; }
static void fake_unsubscribe(const char*, EVENT_CONSUME cb) { if (g_nexus_combat == cb) g_nexus_combat = nullptr; }
static void fake_log(ELogLevel, const char* ch, const char* msg) { std::printf("[nexus log] %s: %s\n", ch, msg); }
static void* fake_datalink_get(const char*) { return nullptr; }

struct EvCombatData { cbtevent* ev; ag* src; ag* dst; char* skillname; uint64_t id; uint64_t revision; };

using init_fn = void* (*)(char*, void*, void*, HANDLE, void*, void*, uint32_t);
using mod_init_fn = arcdps_exports* (*)();
using combat_fn = uintptr_t (*)(cbtevent*, ag*, ag*, const char*, uint64_t, uint64_t);

#define REQUIRE(cond) do { if (!(cond)) { std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); return 1; } } while (0)

static cbtevent stab_event(uint32_t trackable, int32_t ms) {
    cbtevent ev{};
    ev.time = timeGetTime();
    ev.is_statechange = CBTS_BUFFAPPLY;
    ev.skillid = 1122;
    ev.value = ms;
    std::memcpy(&ev.pad61, &trackable, 4);
    return ev;
}

static int count_vtx() {
    int vtx = 0;
    ImDrawData* dd = ImGui::GetDrawData();
    for (int i = 0; i < dd->CmdListsCount; ++i) vtx += dd->CmdLists[i]->VtxBuffer.Size;
    return vtx;
}

int main(int argc, char** argv) {
    REQUIRE(argc == 2);
    HMODULE dll = LoadLibraryA(argv[1]);
    REQUIRE(dll);
    REQUIRE(GetProcAddress(dll, "GetAddonDef"));
    auto get_init = (init_fn)GetProcAddress(dll, "get_init_addr");
    auto get_release = (void* (*)())GetProcAddress(dll, "get_release_addr");
    REQUIRE(get_init);
    REQUIRE(get_release);

    ImGui::SetAllocatorFunctions(host_malloc, host_free);
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1920, 1080);
#ifdef SMOKE_ARCDPS
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;   // 1.92: atlas is built on demand
#else
    unsigned char* px; int w, h;
    io.Fonts->GetTexDataAsRGBA32(&px, &w, &h);
#endif

    ag me{"Me", 0x1000, 1, 0, 1, 0};
    char ver[] = "smoke";
    cbtevent remove_all = stab_event(0, 0);
    remove_all.is_statechange = CBTS_BUFFREMOVE_ALL;

#ifdef SMOKE_ARCDPS
    // ---- Plain arcdps, no Nexus ----
    arcdps_exports* ex = ((mod_init_fn)get_init(ver, ctx, nullptr, nullptr, (void*)host_malloc, (void*)host_free, 11))();
    REQUIRE(ex && ex->sig != 0 && ex->size == sizeof(arcdps_exports));
    REQUIRE(ex->imguivers == IMGUI_VERSION_NUM);   // this host is ImGui 1.92.7, like arcdps
    REQUIRE(ex->imguivers == 19270);
    REQUIRE(ex->combat && ex->combat_local && ex->imgui && ex->options_end);
    std::printf("loaded \"%s\" build %s, imgui %u\n", ex->out_name, ex->out_build, ex->imguivers);

    auto arc_frame = [&] {
        io.DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        ((uintptr_t (*)(uint32_t, uint32_t))ex->imgui)(1, 0);
        ImGui::Begin("arcdps options"); ((uintptr_t (*)())ex->options_end)(); ImGui::End();
        ImGui::Render();
        return count_vtx();
    };
    // Baseline after one event so the options' "Combat data" label has settled.
    ((combat_fn)ex->combat)(&remove_all, &me, &me, "Stability", 2, 1);
    arc_frame(); arc_frame();   // ImGui hides auto-resizing windows on their first frame
    const int arc_idle = arc_frame();
    REQUIRE(arc_idle > 0);
    cbtevent ev = stab_event(43, 6000);
    ((combat_fn)ex->combat)(&ev, &me, &me, "Stability", 2, 1);
    ((combat_fn)ex->combat)(nullptr, &me, &me, nullptr, 0, 1);   // agent notification must not crash
    REQUIRE(arc_frame() > arc_idle);
    for (int i = 0; i < 5; ++i) arc_frame();
    ((uintptr_t (*)())get_release())();
#else
    // ---- Phase 1: Nexus alone (combat via ArcDPS Integration events) ----
    auto* def = ((AddonDefinition_t* (*)())GetProcAddress(dll, "GetAddonDef"))();
    REQUIRE(def && def->APIVersion == NEXUS_API_VERSION && def->Load && def->Unload);
    static AddonAPI_t api{};
    api.ImguiContext = ctx;
    api.ImguiMalloc = (void*)host_malloc;
    api.ImguiFree = (void*)host_free;
    api.GUI_Register = fake_gui_register;
    api.GUI_Deregister = fake_gui_deregister;
    api.Events_Subscribe = fake_subscribe;
    api.Events_Unsubscribe = fake_unsubscribe;
    api.Log = fake_log;
    api.DataLink_Get = fake_datalink_get;
    def->Load(&api);
    REQUIRE(g_nexus_render && g_nexus_options && g_nexus_combat);

    auto nexus_frame = [&] {
        io.DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        g_nexus_render();
        ImGui::Begin("nexus options"); g_nexus_options(); ImGui::End();
        ImGui::Render();
        return count_vtx();
    };
    auto nexus_event = [&](cbtevent e) {
        EvCombatData d{&e, &me, &me, nullptr, 2, 1};
        g_nexus_combat(&d);
    };

    // Baseline is taken after one event so the options' "Combat data" label has settled;
    // from then on the vertex count only changes when the countdown appears.
    nexus_event(remove_all);
    nexus_frame(); nexus_frame();   // ImGui hides auto-resizing windows on their first frame
    const int nexus_idle = nexus_frame();
    REQUIRE(nexus_idle > 0);        // options menu + dimmed icon, drawn through Nexus

    nexus_event(stab_event(7, 4000));
    REQUIRE(nexus_frame() > nexus_idle);   // countdown appeared via ArcDPS Integration events
    nexus_event(remove_all);
    REQUIRE(nexus_frame() == nexus_idle);

    // ---- Phase 2: arcdps loads the same DLL too; Nexus keeps the UI, arcdps feeds combat ----
    // arcdps' ImGui context is a different version; with Nexus in charge it must never be touched.
    void* poison_ctx = (void*)0x1;
    arcdps_exports* ex = ((mod_init_fn)get_init(ver, poison_ctx, nullptr, nullptr, nullptr, nullptr, 11))();
    REQUIRE(ex && ex->sig != 0 && ex->size == sizeof(arcdps_exports));
    REQUIRE(ex->imguivers == 19270);           // arcdps' ImGui version, so it raises no mismatch warning
    REQUIRE(ex->combat && ex->combat_local);
    REQUIRE(!ex->imgui && !ex->options_end);   // no arcdps options tab while Nexus manages us
    std::printf("loaded \"%s\" build %s, imgui %u\n", ex->out_name, ex->out_build, ex->imguivers);

    nexus_event(stab_event(8, 4000));
    REQUIRE(nexus_frame() == nexus_idle);      // redundant Nexus events are dropped now
    cbtevent ev = stab_event(42, 6000);
    ((combat_fn)ex->combat)(&ev, &me, &me, "Stability", 2, 1);
    ((combat_fn)ex->combat)(nullptr, &me, &me, nullptr, 0, 1);   // agent notification must not crash
    REQUIRE(nexus_frame() > nexus_idle);       // arcdps' direct feed shows up in the Nexus-drawn overlay
    ((uintptr_t (*)())get_release())();

    // ---- Phase 3: arcdps gone again; Nexus carries on ----
    nexus_event(remove_all);
    REQUIRE(nexus_frame() == nexus_idle);
    nexus_event(stab_event(9, 4000));
    REQUIRE(nexus_frame() > nexus_idle);

    // ---- Phase 4: a signature file dropped beside the DLL is picked up without a reload ----
    // (In this process the "game exe" is the smoke host, so the built-in signature cannot
    // resolve; the status text is what we watch.)
    auto status = (const char* (*)())GetProcAddress(dll, "boon_magnifier_status");
    REQUIRE(status);
    std::string sig_path = argv[1];
    sig_path = sig_path.substr(0, sig_path.find_last_of("\\/") + 1) + "arcdps_boon_magnifier_sigs.ini";
    std::remove(sig_path.c_str());
    std::printf("signatures at load: %s\n", status());
    REQUIRE(std::string(status()).find("enabled=0") == std::string::npos);
    { std::FILE* f = std::fopen(sig_path.c_str(), "wb"); REQUIRE(f); std::fputs("enabled=0\n", f); std::fclose(f); }
    Sleep(1100);                    // the file is checked about once a second, from the render callback
    nexus_frame();
    std::printf("signatures after drop: %s\n", status());
    REQUIRE(std::string(status()).find("enabled=0") != std::string::npos);
    std::remove(sig_path.c_str());
    Sleep(1100);
    nexus_frame();
    std::printf("signatures after removal: %s\n", status());
    REQUIRE(std::string(status()).find("enabled=0") == std::string::npos);   // back to the built-in one

    def->Unload();
    REQUIRE(!g_nexus_render && !g_nexus_options && !g_nexus_combat);
#endif

    ImGui::DestroyContext(ctx);
    FreeLibrary(dll);
    std::printf("smoke test OK\n");
    return 0;
}
