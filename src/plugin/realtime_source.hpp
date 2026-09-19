#pragma once
#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <string>

// Realtime buff source: resolves signatures from arcdps_boon_magnifier_sigs.ini
// against the running game exe and installs the hooks declared in realtime_gw2.cpp.
// Fail-safe by design: no file, enabled=0, a build mismatch, or any signature that
// doesn't resolve to exactly one place means nothing is installed and the overlay
// keeps using arcdps alone. Status is shown in the Diagnostics panel.
namespace plugin {

// Nexus' bundled MinHook (AddonAPI_t::MinHook_*). Null when running under arcdps alone.
struct HookApi {
    int (__stdcall* create)(void* target, void* detour, void** original);
    int (__stdcall* remove)(void* target);
    int (__stdcall* enable)(void* target);
    int (__stdcall* disable)(void* target);
};

// One function to hook. sig_name is a [section] in the signature file; *original
// receives MinHook's trampoline for calling the real function from the detour.
struct HookSpec {
    const char* sig_name;
    void*       detour;
    void**      original;
};

// What realtime_gw2.cpp provides.
struct Gw2Bindings {
    const HookSpec* hooks;
    size_t          hook_count;
    void (*poll)();          // optional, called once per rendered frame; null if unused
};
const Gw2Bindings& gw2_bindings();

void        realtime_source_init(const HookApi* hooks);   // safe to call once per host load
void        realtime_source_poll();                       // render thread, once per frame
void        realtime_source_shutdown();
std::string realtime_source_status();                     // one-line human summary
// Set by the poll function each frame: which step of its pointer chain it reached.
void        realtime_set_poll_status(const char* status);
std::string realtime_poll_status();
// Secondary diagnostic line (skill-bar recharge list), mirrored to the host log on change.
void        realtime_set_debug_status(const char* status);
std::string realtime_debug_status();
uintptr_t   realtime_resolved(const char* sig_name);      // absolute address, 0 if unresolved

// Reads memory without faulting on a bad pointer (ReadProcessMemory on our own process).
template <typename T>
bool safe_read(uintptr_t address, T& out) {
    SIZE_T got = 0;
    return address != 0 &&
           ReadProcessMemory(GetCurrentProcess(), (LPCVOID)address, &out, sizeof(T), &got) && got == sizeof(T);
}

}  // namespace plugin
