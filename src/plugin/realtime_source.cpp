#include "realtime_source.hpp"
#include <cstdio>
#include <fstream>
#include <mutex>
#include <sstream>
#include <vector>
#include "resource.h"
#include "sigscan.hpp"
#include "state.hpp"

namespace plugin {

namespace {

struct Resolved { std::string name; uintptr_t address; };

std::mutex g_mtx;
bool g_initialised = false;
std::string g_status = "not started";
std::vector<Resolved> g_resolved;      // every signature in the file, resolved
std::vector<void*> g_installed;        // hook targets currently installed
HookApi g_hooks{};

std::string sig_path_next_to_dll() {
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(g_self, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    std::string path(buf, n);
    size_t slash = path.find_last_of("\\/");
    path.resize(slash == std::string::npos ? 0 : slash + 1);
    return path + "arcdps_boon_magnifier_sigs.ini";
}

// The game's main module as one image: base, size, and the union of its executable sections.
struct ImageInfo {
    const uint8_t* base = nullptr;
    size_t size = 0;
    size_t code_begin = 0, code_end = 0;
    uint32_t timestamp = 0;
};

bool describe_main_image(ImageInfo& out) {
    HMODULE exe = GetModuleHandleA(nullptr);
    if (!exe) return false;
    auto* dos = (const IMAGE_DOS_HEADER*)exe;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto* nt = (const IMAGE_NT_HEADERS*)((const uint8_t*)exe + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    out.base = (const uint8_t*)exe;
    out.size = nt->OptionalHeader.SizeOfImage;
    out.timestamp = nt->FileHeader.TimeDateStamp;

    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    size_t lo = SIZE_MAX, hi = 0;
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        if (!(sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        size_t b = sec[i].VirtualAddress;
        size_t e = b + sec[i].Misc.VirtualSize;
        if (b < lo) lo = b;
        if (e > hi) hi = e;
    }
    if (hi == 0 || hi > out.size) return false;
    out.code_begin = lo;
    out.code_end = hi;
    return true;
}

void uninstall_locked() {
    for (void* target : g_installed) {
        if (g_hooks.disable) g_hooks.disable(target);
        if (g_hooks.remove) g_hooks.remove(target);
    }
    g_installed.clear();
}

}  // namespace

uintptr_t realtime_resolved(const char* sig_name) {
    std::lock_guard<std::mutex> lock(g_mtx);
    for (const Resolved& r : g_resolved)
        if (r.name == sig_name) return r.address;
    return 0;
}

void realtime_source_init(const HookApi* hooks) {
    std::lock_guard<std::mutex> lock(g_mtx);
    if (g_initialised) return;
    g_initialised = true;
    if (hooks) g_hooks = *hooks;

    // A signature file beside the DLL overrides the one compiled in (signatures/ in the repo).
    std::string text, origin;
    {
        std::ifstream f(sig_path_next_to_dll(), std::ios::binary);
        if (f) {
            std::stringstream ss;
            ss << f.rdbuf();
            text = ss.str();
            origin = "file";
        } else if (HRSRC res = FindResourceA(g_self, MAKEINTRESOURCEA(IDR_DEFAULT_SIGS), (LPCSTR)RT_RCDATA)) {
            if (HGLOBAL mem = LoadResource(g_self, res))
                if (const char* data = (const char*)LockResource(mem))
                    text.assign(data, SizeofResource(g_self, res));
            origin = "built-in";
        }
    }
    if (text.empty()) { g_status = "disabled: no signature file beside the DLL and none built in"; return; }

    core::SigFile file;
    std::string err;
    if (!core::parse_sigfile(text, file, err)) { g_status = "disabled: signature file " + err; return; }
    if (!file.enabled) { g_status = "disabled: enabled=0 in signature file"; return; }

    ImageInfo img;
    if (!describe_main_image(img)) { g_status = "disabled: could not read the game's PE headers"; return; }
    if (file.pe_timestamp && file.pe_timestamp != img.timestamp) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "disabled: signatures are for exe timestamp 0x%08X, running 0x%08X (game updated? regenerate)",
                      file.pe_timestamp, img.timestamp);
        g_status = buf;
        return;
    }
    if (file.pe_size_of_image && file.pe_size_of_image != img.size) {
        g_status = "disabled: exe SizeOfImage differs from the signature file (game updated? regenerate)";
        return;
    }

    // Resolve everything first; one failure aborts the whole thing so state is never half-hooked.
    std::vector<Resolved> resolved;
    for (const core::Signature& sig : file.sigs) {
        size_t off = 0;
        core::ResolveResult r = core::resolve(img.base, img.size, img.code_begin, img.code_end - img.code_begin, sig, off);
        const char* why = r == core::ResolveResult::NotFound ? "not found" :
                          r == core::ResolveResult::Ambiguous ? "matches more than once" :
                          r == core::ResolveResult::OutOfRange ? "resolves outside the image" : nullptr;
        if (why) { g_status = "disabled: signature [" + sig.name + "] " + why; return; }
        resolved.push_back({sig.name, (uintptr_t)img.base + off});
    }
    g_resolved = std::move(resolved);

    const Gw2Bindings& b = gw2_bindings();
    if (b.hook_count && !g_hooks.create) {
        g_status = "resolved " + std::to_string(g_resolved.size()) + " signature(s); hooks need Nexus (MinHook unavailable)";
        return;
    }
    for (size_t i = 0; i < b.hook_count; ++i) {
        const HookSpec& h = b.hooks[i];
        uintptr_t target = 0;
        for (const Resolved& r : g_resolved) if (r.name == h.sig_name) target = r.address;
        if (!target) { uninstall_locked(); g_status = std::string("disabled: hook needs signature [") + h.sig_name + "] which the file lacks"; return; }
        if (g_hooks.create((void*)target, h.detour, h.original) != 0 || g_hooks.enable((void*)target) != 0) {
            g_hooks.remove((void*)target);
            uninstall_locked();
            g_status = std::string("disabled: MinHook failed on [") + h.sig_name + "]";
            return;
        }
        g_installed.push_back((void*)target);
    }
    if (b.poll) realtime_set_stale_after(1000);   // a poller that stops reporting is dropped after 1s
    g_status = "active: " + std::to_string(g_resolved.size()) + " signature(s) (" + origin + "), " +
               std::to_string(g_installed.size()) + " hook(s)" + (b.poll ? ", polling" : "");
}

void realtime_source_poll() {
    const Gw2Bindings& b = gw2_bindings();
    if (!b.poll) return;
    {
        std::lock_guard<std::mutex> lock(g_mtx);
        if (g_resolved.empty()) return;
    }
    b.poll();
}

void realtime_source_shutdown() {
    std::lock_guard<std::mutex> lock(g_mtx);
    uninstall_locked();
    g_resolved.clear();
    g_initialised = false;
    g_status = "stopped";
    realtime_disconnect();
}

std::string realtime_source_status() {
    std::lock_guard<std::mutex> lock(g_mtx);
    return g_status;
}

static const char* g_poll_status = "not polled yet";   // pointers to string literals only

void realtime_set_poll_status(const char* status) { g_poll_status = status; }

std::string realtime_poll_status() { return g_poll_status; }

static const char* g_debug_status = "";

void realtime_set_debug_status(const char* status) { g_debug_status = status; }

std::string realtime_debug_status() { return g_debug_status; }

}  // namespace plugin
