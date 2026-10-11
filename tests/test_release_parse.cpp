#include "doctest.h"
#include "release_parse.hpp"

using namespace core;

// A trimmed copy of what api.github.com/repos/.../releases/latest returns.
static const char* kRelease = R"json({
  "url": "https://api.github.com/repos/mutinylaboratories/arcdps-boon-magnifier/releases/1",
  "tag_name": "v0.1.2",
  "name": "v0.1.2",
  "assets": [
    {"name": "arcdps_boon_magnifier.dll",
     "browser_download_url": "https://github.com/mutinylaboratories/arcdps-boon-magnifier/releases/download/v0.1.2/arcdps_boon_magnifier.dll"},
    {"name": "arcdps_boon_magnifier.dll.sha256",
     "browser_download_url": "https://github.com/mutinylaboratories/arcdps-boon-magnifier/releases/download/v0.1.2/arcdps_boon_magnifier.dll.sha256"},
    {"name": "arcdps_boon_magnifier_sigs.ini",
     "browser_download_url": "https://github.com/mutinylaboratories/arcdps-boon-magnifier/releases/download/v0.1.2/arcdps_boon_magnifier_sigs.ini"}
  ],
  "body": "Built for Guild Wars 2 build 208039 (tag_name is not a key here)"
})json";

TEST_CASE("version_newer compares tags numerically, with or without the v") {
    CHECK(version_newer("v0.1.2", 0, 1, 1));
    CHECK(version_newer("0.1.2", 0, 1, 1));
    CHECK(!version_newer("v0.1.2", 0, 1, 2));
    CHECK(!version_newer("v0.1.2", 0, 1, 3));
    CHECK(version_newer("v0.2.0", 0, 1, 9));
    CHECK(version_newer("v1.0", 0, 9, 9));       // missing patch = 0
    CHECK(version_newer("v0.1.10", 0, 1, 9));    // not a string compare
    CHECK(!version_newer("latest", 0, 0, 0));    // garbage never counts as newer
}

TEST_CASE("json_string finds the tag and asset_url picks assets by suffix") {
    std::string json = kRelease;
    CHECK(json_string(json, "tag_name") == "v0.1.2");
    CHECK(json_string(json, "missing").empty());
    CHECK(asset_url(json, ".dll") == "https://github.com/mutinylaboratories/arcdps-boon-magnifier/releases/download/v0.1.2/arcdps_boon_magnifier.dll");
    CHECK(asset_url(json, ".dll.sha256").size() > 0);
    CHECK(asset_url(json, "_sigs.ini").size() > 0);
    CHECK(asset_url(json, ".zip").empty());
    CHECK(asset_url("{\"tag_name\":\"v9.9.9\"}", ".dll").empty());   // a release without assets
}
