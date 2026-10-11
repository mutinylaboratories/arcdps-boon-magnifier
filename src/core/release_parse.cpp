#include "release_parse.hpp"
#include <cstdio>
#include <cstring>

namespace core {

bool version_newer(const std::string& tag, int major, int minor, int patch) {
    int a = 0, b = 0, c = 0;
    const char* s = tag.c_str();
    if (*s == 'v' || *s == 'V') ++s;
    if (std::sscanf(s, "%d.%d.%d", &a, &b, &c) < 2) return false;
    if (a != major) return a > major;
    if (b != minor) return b > minor;
    return c > patch;
}

std::string json_string(const std::string& json, const char* key, size_t from) {
    const std::string needle = "\"" + std::string(key) + "\"";
    size_t at = json.find(needle, from);
    if (at == std::string::npos) return {};
    size_t colon = json.find(':', at + needle.size());
    if (colon == std::string::npos) return {};
    at = json.find('"', colon + 1);
    if (at == std::string::npos) return {};
    size_t end = json.find('"', at + 1);
    return end == std::string::npos ? std::string() : json.substr(at + 1, end - at - 1);
}

std::string asset_url(const std::string& json, const char* suffix) {
    const size_t n = std::strlen(suffix);
    for (size_t from = 0;;) {
        size_t at = json.find("\"browser_download_url\"", from);
        if (at == std::string::npos) return {};
        std::string url = json_string(json, "browser_download_url", at);
        if (url.size() >= n && url.compare(url.size() - n, n, suffix) == 0) return url;
        from = at + 1;
    }
}

}  // namespace core
