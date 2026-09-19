#include "doctest.h"
#include "config.hpp"

using namespace core;

TEST_CASE("config round-trips through serialize/parse") {
    Config a;
    a.enabled = false;
    a.locked = true;
    a.icon_size = 192;
    a.icon_style = ICON_TEXTURE;
    a.text_pos = TEXT_RIGHT;
    a.text_scale = 1.5f;
    a.show_stacks = false;
    a.warn_seconds = 3.5f;
    a.opacity = 0.8f;

    Config b;
    parse(b, serialize(a));
    CHECK(b.enabled == false);
    CHECK(b.locked == true);
    CHECK(b.icon_size == 192);
    CHECK(b.icon_style == ICON_TEXTURE);
    CHECK(b.text_pos == TEXT_RIGHT);
    CHECK(b.text_scale == doctest::Approx(1.5f));
    CHECK(b.show_stacks == false);
    CHECK(b.warn_seconds == doctest::Approx(3.5f));
    CHECK(b.opacity == doctest::Approx(0.8f));
}

TEST_CASE("parse tolerates junk, CRLF and comments, and clamps values") {
    Config c;
    parse(c, "# comment\r\nicon_size=99999\r\nnonsense\r\nunknown=3\r\ntext_pos=abc\r\nopacity=-4\r\n");
    CHECK(c.icon_size == kIconSizeMax);
    CHECK(c.text_pos == TEXT_OVER);
    CHECK(c.opacity == doctest::Approx(0.1f));
    CHECK(c.enabled == true);
}

TEST_CASE("tracked boon list round-trips and tolerates junk") {
    Config a;
    a.boons = {1122, 1187, 740};
    Config b;
    parse(b, serialize(a));
    CHECK(b.boons == std::vector<uint32_t>{1122, 1187, 740});
    Config c;
    parse(c, "boons=abc,0,\n");
    CHECK(c.boons == std::vector<uint32_t>{1122});   // nothing valid: default kept
    parse(c, "boons= 743 ,x,725\n");
    CHECK(c.boons == std::vector<uint32_t>{743, 725});
}
