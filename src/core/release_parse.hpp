#pragma once
#include <cstddef>
#include <string>

// The pure part of the self-updater: reading what it needs out of GitHub's "latest release"
// JSON without a JSON library, and comparing release tags with the running version.
namespace core {

// "vX.Y.Z" or "X.Y.Z" (a missing patch counts as 0) is newer than major.minor.patch.
bool version_newer(const std::string& tag, int major, int minor, int patch);

// Value of the first string field named `key` at or after `from`; empty if absent.
std::string json_string(const std::string& json, const char* key, size_t from = 0);

// First asset's browser_download_url whose name ends with `suffix`; empty if none.
std::string asset_url(const std::string& json, const char* suffix);

}  // namespace core
