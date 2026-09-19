#pragma once
#include <cstdint>
#include <vector>
#include "boon_tracker.hpp"

// The standard boons: buff skill ids, how they stack, and where their icons live.
namespace core {

struct BoonDef {
    uint32_t    id;
    const char* name;
    const char* abbrev;        // 2-3 letters for the drawn fallback icon
    Stacking    stacking;
    int         max_stacks;
    const char* icon_path;     // path on wiki.guildwars2.com (32x32 png), fetched by the Nexus texture API
    unsigned    color;         // 0xRRGGBB tint for the fallback icon
};

constexpr uint32_t kBuffStability = 1122;

inline const std::vector<BoonDef>& boon_catalogue() {
    static const std::vector<BoonDef> boons = {
        {1122,  "Stability",    "STB", Stacking::Intensity, 25, "/images/a/ae/Stability.png",    0xE89226},
        {740,   "Might",        "MGT", Stacking::Intensity, 25, "/images/7/7c/Might.png",        0xD9432F},
        {725,   "Fury",         "FRY", Stacking::Duration,   9, "/images/4/46/Fury.png",         0xE04A2A},
        {1187,  "Quickness",    "QCK", Stacking::Duration,   5, "/images/b/b4/Quickness.png",    0xB44BD6},
        {30328, "Alacrity",     "ALC", Stacking::Duration,   9, "/images/4/4c/Alacrity.png",     0x7E5BD1},
        {717,   "Protection",   "PRT", Stacking::Duration,   5, "/images/6/6c/Protection.png",   0x3E8ED8},
        {718,   "Regeneration", "RGN", Stacking::Duration,   5, "/images/5/53/Regeneration.png", 0x4CB86A},
        {726,   "Vigor",        "VGR", Stacking::Duration,   5, "/images/f/f4/Vigor.png",        0x68C9A6},
        {743,   "Aegis",        "AEG", Stacking::Duration,   5, "/images/e/e5/Aegis.png",        0x5FA8E8},
        {719,   "Swiftness",    "SWF", Stacking::Duration,   9, "/images/a/af/Swiftness.png",    0xE0C85A},
        {26980, "Resistance",   "RES", Stacking::Duration,   5, "/images/4/4b/Resistance.png",   0xD8A94A},
        {873,   "Resolution",   "RSL", Stacking::Duration,   5, "/images/0/06/Resolution.png",   0xE8DF9A},
    };
    return boons;
}

inline const BoonDef* find_boon(uint32_t id) {
    for (const BoonDef& b : boon_catalogue())
        if (b.id == id) return &b;
    return nullptr;
}

}  // namespace core
