#include "sigscan.hpp"
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace core {

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool parse_pattern(const std::string& text, Pattern& out) {
    out.bytes.clear();
    out.mask.clear();
    std::istringstream in(text);
    std::string tok;
    bool any_fixed = false;
    while (in >> tok) {
        if (tok == "?" || tok == "??") {
            out.bytes.push_back(0);
            out.mask.push_back(0);
            continue;
        }
        if (tok.size() != 2) return false;
        int hi = hex_value(tok[0]), lo = hex_value(tok[1]);
        if (hi < 0 || lo < 0) return false;
        out.bytes.push_back((uint8_t)(hi * 16 + lo));
        out.mask.push_back(1);
        any_fixed = true;
    }
    return any_fixed;
}

std::vector<size_t> scan(const uint8_t* data, size_t size, const Pattern& p, size_t max_matches) {
    std::vector<size_t> hits;
    const size_t n = p.size();
    if (!data || n == 0 || size < n || max_matches == 0) return hits;

    // Anchor on the first fixed byte so memchr does the bulk of the walking.
    size_t anchor = 0;
    while (anchor < n && !p.mask[anchor]) ++anchor;
    if (anchor == n) return hits;

    const size_t last = size - n;   // last valid match offset
    size_t pos = 0;
    while (pos <= last) {
        const void* f = std::memchr(data + pos + anchor, p.bytes[anchor], last - pos + 1);
        if (!f) break;
        pos = (size_t)((const uint8_t*)f - data) - anchor;
        bool ok = true;
        for (size_t i = 0; i < n; ++i)
            if (p.mask[i] && data[pos + i] != p.bytes[i]) { ok = false; break; }
        if (ok) {
            hits.push_back(pos);
            if (hits.size() >= max_matches) break;
        }
        ++pos;
    }
    return hits;
}

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static bool parse_int(const std::string& s, int64_t& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    out = std::strtoll(s.c_str(), &end, 0);   // base 0: decimal, 0x hex, leading '-'
    return end && *end == 0;
}

bool parse_sigfile(const std::string& text, SigFile& out, std::string& error) {
    out = SigFile{};
    std::istringstream in(text);
    std::string raw;
    Signature* cur = nullptr;
    int line_no = 0;
    auto fail = [&](const std::string& why) {
        error = "line " + std::to_string(line_no) + ": " + why;
        return false;
    };

    while (std::getline(in, raw)) {
        ++line_no;
        size_t comment = raw.find_first_of("#;");
        std::string line = trim(comment == std::string::npos ? raw : raw.substr(0, comment));
        if (line.empty()) continue;

        if (line.front() == '[') {
            if (line.back() != ']' || line.size() < 3) return fail("bad section header");
            out.sigs.emplace_back();
            cur = &out.sigs.back();
            cur->name = trim(line.substr(1, line.size() - 2));
            continue;
        }

        size_t eq = line.find('=');
        if (eq == std::string::npos) return fail("expected key=value");
        const std::string key = trim(line.substr(0, eq));
        const std::string val = trim(line.substr(eq + 1));
        int64_t num = 0;

        if (!cur) {
            if (!parse_int(val, num)) return fail("expected a number for " + key);
            if (key == "enabled") out.enabled = num != 0;
            else if (key == "pe_timestamp") out.pe_timestamp = (uint32_t)num;
            else if (key == "pe_size_of_image") out.pe_size_of_image = (uint32_t)num;
            else return fail("unknown key " + key);
        } else if (key == "pattern") {
            if (!parse_pattern(val, cur->pattern)) return fail("malformed pattern");
        } else if (key == "follow_rel32") {
            if (!parse_int(val, num)) return fail("expected a number for follow_rel32");
            cur->follow_rel32 = (int)num;
        } else if (key == "offset") {
            if (!parse_int(val, cur->offset)) return fail("expected a number for offset");
        } else {
            return fail("unknown key " + key);
        }
    }

    for (const Signature& s : out.sigs) {
        if (s.pattern.size() == 0) { error = "[" + s.name + "] has no pattern"; return false; }
        if (s.follow_rel32 >= 0 && (size_t)s.follow_rel32 + 4 > s.pattern.size()) {
            error = "[" + s.name + "] follow_rel32 lies outside the pattern";
            return false;
        }
    }
    return true;
}

ResolveResult resolve(const uint8_t* image, size_t image_size, size_t scan_begin, size_t scan_size,
                      const Signature& sig, size_t& out_offset) {
    if (!image || scan_begin > image_size || scan_size > image_size - scan_begin)
        return ResolveResult::OutOfRange;
    std::vector<size_t> hits = scan(image + scan_begin, scan_size, sig.pattern, 2);
    if (hits.empty()) return ResolveResult::NotFound;
    if (hits.size() > 1) return ResolveResult::Ambiguous;

    int64_t at = (int64_t)(scan_begin + hits[0]);
    if (sig.follow_rel32 >= 0) {
        int64_t field = at + sig.follow_rel32;
        if (field < 0 || (uint64_t)field + 4 > image_size) return ResolveResult::OutOfRange;
        int32_t rel;
        std::memcpy(&rel, image + field, sizeof(rel));
        at = field + 4 + rel;
    }
    at += sig.offset;
    if (at < 0 || (uint64_t)at >= image_size) return ResolveResult::OutOfRange;
    out_offset = (size_t)at;
    return ResolveResult::Ok;
}

}  // namespace core
