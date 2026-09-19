#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace core {

enum IconStyle { ICON_VECTOR = 0, ICON_TEXTURE = 1 };
enum DigitStyle { DIGITS_SEGMENT = 0, DIGITS_FONT = 1 };
enum TextPos { TEXT_OVER = 0, TEXT_BELOW = 1, TEXT_RIGHT = 2 };

constexpr int kIconSizeMin = 24;
constexpr int kIconSizeMax = 384;
constexpr int kIconSizePresets[] = {48, 64, 96, 128, 192, 256};

struct Config {
    std::vector<uint32_t> boons = {1122};   // buff ids to show, one overlay each (see core/boons.hpp)
    bool  enabled = true;
    bool  locked = false;          // click-through and immovable when set
    bool  show_inactive = true;    // dimmed icon while the boon is down
    int   icon_size = 128;         // px
    int   icon_style = ICON_VECTOR;
    int   text_pos = TEXT_OVER;
    int   digit_style = DIGITS_SEGMENT;
    float text_scale = 1.0f;       // multiplier on the icon-relative font size
    bool  show_decimals = true;    // tenths of a second below 10s
    bool  show_stacks = true;
    bool  project_pulses = true;   // extend the countdown to a pulsing field's end once its pattern is recognised
    bool  show_squad = false;      // squad stability window (needs the realtime source)
    bool  show_uptime = false;     // small "up 12s" readout: how long the boon has been continuously active
    bool  show_sweep = true;       // darken the icon from the top as time runs out
    float warn_seconds = 2.0f;     // countdown turns red below this; 0 disables
    float opacity = 1.0f;
};

void clamp(Config& c);
std::string serialize(const Config& c);
// Unknown keys and malformed lines are skipped; missing keys keep their current value.
void parse(Config& c, const std::string& text);

bool load_config(Config& c, const char* path);
bool save_config(const Config& c, const char* path);

}  // namespace core
