#include "config.hpp"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace core {

void clamp(Config& c) {
    if (c.boons.empty()) c.boons = {1122};
    c.icon_size = std::clamp(c.icon_size, kIconSizeMin, kIconSizeMax);
    c.icon_style = std::clamp(c.icon_style, (int)ICON_VECTOR, (int)ICON_TEXTURE);
    c.text_pos = std::clamp(c.text_pos, (int)TEXT_OVER, (int)TEXT_RIGHT);
    c.digit_style = std::clamp(c.digit_style, (int)DIGITS_SEGMENT, (int)DIGITS_FONT);
    c.text_scale = std::clamp(c.text_scale, 0.25f, 3.0f);
    c.warn_seconds = std::clamp(c.warn_seconds, 0.0f, 30.0f);
    c.opacity = std::clamp(c.opacity, 0.1f, 1.0f);
}

std::string serialize(const Config& c) {
    std::ostringstream o;
    o << "boons=";
    for (size_t i = 0; i < c.boons.size(); ++i) o << (i ? "," : "") << c.boons[i];
    o << "\n"
      << "enabled=" << c.enabled << "\n"
      << "locked=" << c.locked << "\n"
      << "show_inactive=" << c.show_inactive << "\n"
      << "icon_size=" << c.icon_size << "\n"
      << "icon_style=" << c.icon_style << "\n"
      << "text_pos=" << c.text_pos << "\n"
      << "digit_style=" << c.digit_style << "\n"
      << "text_scale=" << c.text_scale << "\n"
      << "show_decimals=" << c.show_decimals << "\n"
      << "show_stacks=" << c.show_stacks << "\n"
      << "show_sweep=" << c.show_sweep << "\n"
      << "warn_seconds=" << c.warn_seconds << "\n"
      << "opacity=" << c.opacity << "\n";
    return o.str();
}

void parse(Config& c, const std::string& text) {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t eq = line.find('=');
        if (eq == std::string::npos || line[0] == '#' || line[0] == ';') continue;
        std::string key = line.substr(0, eq);
        const char* val = line.c_str() + eq + 1;
        if (key == "boons") {
            std::vector<uint32_t> ids;
            std::istringstream parts(val);
            std::string tok;
            while (std::getline(parts, tok, ',')) {
                char* e = nullptr;
                unsigned long id = std::strtoul(tok.c_str(), &e, 10);
                if (e != tok.c_str() && id) ids.push_back((uint32_t)id);
            }
            if (!ids.empty()) c.boons = ids;
            continue;
        }
        char* end = nullptr;
        double num = std::strtod(val, &end);
        if (end == val) continue;   // not a number

        if      (key == "enabled")       c.enabled = num != 0;
        else if (key == "locked")        c.locked = num != 0;
        else if (key == "show_inactive") c.show_inactive = num != 0;
        else if (key == "icon_size")     c.icon_size = (int)num;
        else if (key == "icon_style")    c.icon_style = (int)num;
        else if (key == "text_pos")      c.text_pos = (int)num;
        else if (key == "digit_style")   c.digit_style = (int)num;
        else if (key == "text_scale")    c.text_scale = (float)num;
        else if (key == "show_decimals") c.show_decimals = num != 0;
        else if (key == "show_stacks")   c.show_stacks = num != 0;
        else if (key == "show_uptime")   c.show_uptime = num != 0;
        else if (key == "show_squad")    c.show_squad = num != 0;
        else if (key == "project_pulses") c.project_pulses = num != 0;
        else if (key == "show_sweep")    c.show_sweep = num != 0;
        else if (key == "warn_seconds")  c.warn_seconds = (float)num;
        else if (key == "opacity")       c.opacity = (float)num;
    }
    clamp(c);
}

bool load_config(Config& c, const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    parse(c, ss.str());
    return true;
}

bool save_config(const Config& c, const char* path) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << serialize(c);
    return (bool)f;
}

}  // namespace core
