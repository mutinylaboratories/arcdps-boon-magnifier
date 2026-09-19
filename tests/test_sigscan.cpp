#include "doctest.h"
#include <cstring>
#include "sigscan.hpp"

using namespace core;

TEST_CASE("pattern parsing") {
    Pattern p;
    CHECK(parse_pattern("48 8B ?? 05 ? aa", p));
    CHECK(p.size() == 6);
    CHECK(p.bytes[0] == 0x48);
    CHECK(p.mask[2] == 0);
    CHECK(p.mask[4] == 0);
    CHECK(p.bytes[5] == 0xAA);
    CHECK_FALSE(parse_pattern("", p));
    CHECK_FALSE(parse_pattern("?? ??", p));
    CHECK_FALSE(parse_pattern("48 8", p));
    CHECK_FALSE(parse_pattern("48 GG", p));
}

TEST_CASE("scan finds matches with wildcards, honours max_matches, and never runs off the end") {
    const uint8_t data[] = {0x00, 0x48, 0x8B, 0x01, 0x05, 0x48, 0x8B, 0x02, 0x05, 0x48, 0x8B};
    Pattern p;
    REQUIRE(parse_pattern("48 8B ?? 05", p));
    auto hits = scan(data, sizeof(data), p, 10);
    REQUIRE(hits.size() == 2);
    CHECK(hits[0] == 1);
    CHECK(hits[1] == 5);
    CHECK(scan(data, sizeof(data), p, 1).size() == 1);
    Pattern tail;
    REQUIRE(parse_pattern("48 8B", tail));
    CHECK(scan(data, sizeof(data), tail, 10).size() == 3);   // last match ends exactly at the end
    CHECK(scan(data, 2, p, 10).empty());                       // buffer shorter than the pattern
}

TEST_CASE("signature file parsing") {
    SigFile f;
    std::string err;
    const char* text =
        "# generated\n"
        "enabled=1\n"
        "pe_timestamp=0x66f1a2b3\n"
        "pe_size_of_image=0x2b4f000 ; trailing comment\n"
        "\n"
        "[self_buff_apply]\n"
        "pattern=E8 ?? ?? ?? ?? 48 8B D8\n"
        "follow_rel32=1\n"
        "offset=0x10\n"
        "[buff_list]\n"
        "pattern=48 8B 05 ?? ?? ?? ??\n"
        "follow_rel32=3\n";
    REQUIRE(parse_sigfile(text, f, err));
    CHECK(f.enabled);
    CHECK(f.pe_timestamp == 0x66f1a2b3u);
    CHECK(f.pe_size_of_image == 0x2b4f000u);
    REQUIRE(f.sigs.size() == 2);
    CHECK(f.sigs[0].name == "self_buff_apply");
    CHECK(f.sigs[0].follow_rel32 == 1);
    CHECK(f.sigs[0].offset == 0x10);
    CHECK(f.sigs[1].pattern.size() == 7);
    CHECK(f.sigs[1].offset == 0);

    CHECK_FALSE(parse_sigfile("[a]\npattern=zz\n", f, err));
    CHECK_FALSE(parse_sigfile("[a]\n", f, err));                         // no pattern
    CHECK_FALSE(parse_sigfile("[a]\npattern=48 8B\nfollow_rel32=1\n", f, err));   // rel32 past the end
    CHECK_FALSE(parse_sigfile("bogus=1\n", f, err));
    CHECK_FALSE(parse_sigfile("enabled=1\n[a]\nnope\n", f, err));
    CHECK_FALSE(err.empty());
    CHECK(parse_sigfile("", f, err));   // empty file is valid: nothing enabled, nothing to resolve
    CHECK_FALSE(f.enabled);
    CHECK(f.sigs.empty());
}

TEST_CASE("resolve: direct, rel32-follow, ambiguity and range checks") {
    // Fake image: [0..16) junk, [16..48) "code" containing a call whose rel32 lands at 40.
    uint8_t img[64] = {};
    std::memset(img, 0x90, sizeof(img));
    img[20] = 0xE8;                        // call rel32 at 20; rel = target - (20+5)
    int32_t rel = 40 - 25;
    std::memcpy(&img[21], &rel, 4);
    img[25] = 0x48; img[26] = 0x8B; img[27] = 0xD8;
    img[40] = 0xCC;

    Signature s;
    REQUIRE(parse_pattern("E8 ?? ?? ?? ?? 48 8B D8", s.pattern));
    size_t out = 0;
    CHECK(resolve(img, sizeof(img), 16, 32, s, out) == ResolveResult::Ok);
    CHECK(out == 20);

    s.follow_rel32 = 1;
    CHECK(resolve(img, sizeof(img), 16, 32, s, out) == ResolveResult::Ok);
    CHECK(out == 40);
    CHECK(img[out] == 0xCC);

    s.offset = 100;   // walks off the image
    CHECK(resolve(img, sizeof(img), 16, 32, s, out) == ResolveResult::OutOfRange);
    s.offset = 0;
    s.follow_rel32 = -1;

    Signature nop;
    REQUIRE(parse_pattern("90 90", nop.pattern));
    CHECK(resolve(img, sizeof(img), 16, 32, nop, out) == ResolveResult::Ambiguous);

    Signature missing;
    REQUIRE(parse_pattern("DE AD BE EF", missing.pattern));
    CHECK(resolve(img, sizeof(img), 16, 32, missing, out) == ResolveResult::NotFound);

    CHECK(resolve(img, sizeof(img), 60, 32, s, out) == ResolveResult::OutOfRange);   // bad scan window
}
