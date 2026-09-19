#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Byte-pattern ("signature") scanning and the signature file format. Pure logic
// working on buffers, so it is unit-tested without the game; the plugin applies it
// to the game's code section at load.
namespace core {

struct Pattern {
    std::vector<uint8_t> bytes;
    std::vector<uint8_t> mask;   // 1 = byte must match, 0 = wildcard
    size_t size() const { return bytes.size(); }
};

// "48 8B ?? 05 ? ? ? ?" - hex byte pairs, "?" or "??" for a wildcard. False if malformed,
// empty, or all wildcards.
bool parse_pattern(const std::string& text, Pattern& out);

// Offsets of matches in data, at most max_matches (2 is enough to tell "unique" from "ambiguous").
std::vector<size_t> scan(const uint8_t* data, size_t size, const Pattern& pattern, size_t max_matches = 2);

struct Signature {
    std::string name;
    Pattern pattern;
    // Resolution, applied in order to the match offset m:
    //   follow_rel32 >= 0: read the int32 at m+follow_rel32 and go to (m+follow_rel32+4)+rel
    //                      (the target of a call/jmp/rip-relative operand inside the match)
    //   then add `offset`.
    int follow_rel32 = -1;
    int64_t offset = 0;
};

struct SigFile {
    bool enabled = false;             // nothing is resolved or hooked unless the file says enabled=1
    uint32_t pe_timestamp = 0;        // identity of the exe the signatures came from; 0 = don't check
    uint32_t pe_size_of_image = 0;    // 0 = don't check
    std::vector<Signature> sigs;
};

// Format (written by tools/binja/make_sigs.py):
//   enabled=1
//   pe_timestamp=0x66f1a2b3
//   pe_size_of_image=0x2b4f000
//   [self_buff_apply]
//   pattern=48 89 5C 24 ?? 57 48 83 EC 20
//   follow_rel32=-1
//   offset=0
// '#' and ';' start comments. Returns false and sets error on the first malformed line.
bool parse_sigfile(const std::string& text, SigFile& out, std::string& error);

enum class ResolveResult { Ok, NotFound, Ambiguous, OutOfRange };

// Resolves sig inside one contiguous image (image/image_size), scanning [scan_begin, scan_begin+scan_size).
// out_offset is relative to image and may land outside the scanned section (a rel32 can leave .text).
ResolveResult resolve(const uint8_t* image, size_t image_size, size_t scan_begin, size_t scan_size,
                      const Signature& sig, size_t& out_offset);

}  // namespace core
