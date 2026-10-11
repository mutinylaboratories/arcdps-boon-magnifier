#include "self_update.hpp"
#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>
#include "release_parse.hpp"
#include "state.hpp"   // g_self, PLUGIN_VERSION_*

namespace plugin {

namespace {

constexpr wchar_t kApiHost[] = L"api.github.com";
constexpr wchar_t kApiPath[] = L"/repos/mutinylaboratories/arcdps-boon-magnifier/releases/latest";

std::mutex g_mtx;
SelfUpdateStatus g_status;
std::thread g_worker;
std::atomic<bool> g_busy{false};

void set_text(const std::string& text) {
    std::lock_guard<std::mutex> lock(g_mtx);
    g_status.text = text;
}

std::string self_path() {
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(g_self, buf, MAX_PATH);
    return (n == 0 || n >= MAX_PATH) ? std::string() : std::string(buf, n);
}

std::wstring widen(const std::string& s) {
    std::wstring w(s.size(), L'\0');
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], (int)w.size());
    w.resize(n > 0 ? n : 0);
    return w;
}

// GET https://host/path. WinHTTP follows https->https redirects itself (release assets live on
// objects.githubusercontent.com); the agent string doubles as GitHub's required User-Agent.
bool https_get(const std::wstring& host, const std::wstring& path, std::string& body, std::string& err) {
    body.clear();
    const std::wstring agent = widen("arcdps_boon_magnifier/" PLUGIN_VERSION);
    HINTERNET session = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) { err = "WinHttpOpen failed"; return false; }
    WinHttpSetTimeouts(session, 10000, 10000, 10000, 30000);
    bool ok = false;
    HINTERNET conn = WinHttpConnect(session, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET req = conn ? WinHttpOpenRequest(conn, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                              WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
    if (req && WinHttpSendRequest(req, L"Accept: application/vnd.github+json\r\n", (DWORD)-1L,
                                  WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(req, nullptr)) {
        DWORD code = 0, size = sizeof(code);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                            &code, &size, WINHTTP_NO_HEADER_INDEX);
        if (code != 200) {
            err = "HTTP " + std::to_string(code);
        } else {
            ok = true;
            for (;;) {
                DWORD avail = 0;
                if (!WinHttpQueryDataAvailable(req, &avail)) { ok = false; err = "read failed"; break; }
                if (avail == 0) break;
                size_t at = body.size();
                body.resize(at + avail);
                DWORD got = 0;
                if (!WinHttpReadData(req, &body[at], avail, &got)) { ok = false; err = "read failed"; break; }
                body.resize(at + got);
            }
        }
    } else {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "request failed (error %lu)", (unsigned long)GetLastError());
        err = buf;
    }
    if (req) WinHttpCloseHandle(req);
    if (conn) WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);
    return ok;
}

// "https://host/path" -> host, path
bool split_url(const std::string& url, std::wstring& host, std::wstring& path) {
    const std::string prefix = "https://";
    if (url.compare(0, prefix.size(), prefix) != 0) return false;
    size_t slash = url.find('/', prefix.size());
    if (slash == std::string::npos) return false;
    host = widen(url.substr(prefix.size(), slash - prefix.size()));
    path = widen(url.substr(slash));
    return true;
}

std::string sha256_hex(const std::string& data) {
    HCRYPTPROV prov = 0;
    HCRYPTHASH hash = 0;
    std::string hex;
    if (CryptAcquireContextA(&prov, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) &&
        CryptCreateHash(prov, CALG_SHA_256, 0, 0, &hash) &&
        CryptHashData(hash, (const BYTE*)data.data(), (DWORD)data.size(), 0)) {
        BYTE digest[32];
        DWORD len = sizeof(digest);
        if (CryptGetHashParam(hash, HP_HASHVAL, digest, &len, 0)) {
            char buf[3];
            for (DWORD i = 0; i < len; ++i) { std::snprintf(buf, sizeof(buf), "%02x", digest[i]); hex += buf; }
        }
    }
    if (hash) CryptDestroyHash(hash);
    if (prov) CryptReleaseContext(prov, 0);
    return hex;
}

bool write_file(const std::string& path, const std::string& data) {
    HANDLE h = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(h, data.data(), (DWORD)data.size(), &written, nullptr) && written == data.size();
    CloseHandle(h);
    return ok;
}

// Replaces `target` with `fresh`: the mapped DLL cannot be overwritten or deleted, but it can be
// renamed out of the way.
bool swap_in(const std::string& fresh, const std::string& target) {
    std::string aside = target + ".old";
    if (!MoveFileExA(target.c_str(), aside.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        aside = target + ".old-" + std::to_string(GetTickCount64());   // the previous .old is still mapped
        if (!MoveFileExA(target.c_str(), aside.c_str(), 0)) return false;
    }
    if (MoveFileExA(fresh.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING)) return true;
    MoveFileExA(aside.c_str(), target.c_str(), 0);   // put the original back
    return false;
}

void worker(bool install) {
    std::string json, err;
    set_text("checking GitHub...");
    if (!https_get(kApiHost, kApiPath, json, err)) { set_text("check failed: " + err); return; }

    const std::string tag = core::json_string(json, "tag_name");
    if (tag.empty()) { set_text("check failed: no tag_name in the release data"); return; }
    const std::string latest = tag[0] == 'v' ? tag.substr(1) : tag;
    const bool newer = core::version_newer(tag, PLUGIN_VERSION_MAJOR, PLUGIN_VERSION_MINOR, PLUGIN_VERSION_PATCH);
    {
        std::lock_guard<std::mutex> lock(g_mtx);
        g_status.latest = latest;
        g_status.available = newer;
        g_status.text = newer ? "v" + latest + " is available (running v" PLUGIN_VERSION ")"
                              : "up to date (v" PLUGIN_VERSION ", latest v" + latest + ")";
    }
    if (!newer || !install) return;

    const std::string dll_url = core::asset_url(json, ".dll");
    if (dll_url.empty()) { set_text("v" + latest + " has no .dll asset"); return; }
    std::wstring host, path;
    std::string dll, sha, ini;
    set_text("downloading v" + latest + "...");
    if (!split_url(dll_url, host, path) || !https_get(host, path, dll, err)) { set_text("download failed: " + err); return; }
    if (dll.size() < 65536 || dll[0] != 'M' || dll[1] != 'Z') { set_text("download failed: not a DLL"); return; }

    // The release publishes the DLL's SHA-256 beside it; a mismatch means a bad download.
    const std::string sha_url = core::asset_url(json, ".dll.sha256");
    if (!sha_url.empty() && split_url(sha_url, host, path) && https_get(host, path, sha, err)) {
        const std::string want = sha.substr(0, 64), have = sha256_hex(dll);
        if (want != have) { set_text("download failed: SHA-256 mismatch"); return; }
    }

    const std::string me = self_path();
    if (me.empty()) { set_text("install failed: cannot locate the DLL"); return; }
    const std::string staged = me + ".update";
    if (!write_file(staged, dll)) { set_text("install failed: cannot write beside the DLL"); return; }
    if (!swap_in(staged, me)) { DeleteFileA(staged.c_str()); set_text("install failed: cannot replace the DLL"); return; }

    // The signature file beside the DLL (if any) overrides the embedded one, so refresh it too.
    const std::string ini_url = core::asset_url(json, "_sigs.ini");
    const std::string ini_path = me.substr(0, me.find_last_of("\\/") + 1) + "arcdps_boon_magnifier_sigs.ini";
    if (!ini_url.empty() && GetFileAttributesA(ini_path.c_str()) != INVALID_FILE_ATTRIBUTES &&
        split_url(ini_url, host, path) && https_get(host, path, ini, err) && write_file(ini_path + ".new", ini))
        MoveFileExA((ini_path + ".new").c_str(), ini_path.c_str(), MOVEFILE_REPLACE_EXISTING);

    std::lock_guard<std::mutex> lock(g_mtx);
    g_status.installed = true;
    g_status.text = "v" + latest + " installed; restart the game to use it";
}

}  // namespace

void self_update_check(bool install) {
    if (g_busy.exchange(true)) return;
    if (g_worker.joinable()) g_worker.join();
    {
        std::lock_guard<std::mutex> lock(g_mtx);
        g_status.busy = true;
    }
    g_worker = std::thread([install] {
        worker(install);
        std::lock_guard<std::mutex> lock(g_mtx);
        g_status.busy = false;
        g_busy = false;
    });
}

SelfUpdateStatus self_update_status() {
    std::lock_guard<std::mutex> lock(g_mtx);
    return g_status;
}

void self_update_shutdown() {
    if (g_worker.joinable()) g_worker.join();   // bounded by the WinHTTP timeouts
}

}  // namespace plugin
