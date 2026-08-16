#include "core/text/Cp949.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>

namespace {

// UTF-8 spellings, kept as escapes so the source itself stays ASCII.
const std::string kHangeulUtf8 = "\xED\x95\x9C\xEA\xB8\x80";        // "hangul", U+D55C U+AE00
const std::string kHangeulCp949 = "\xC7\xD1\xB1\xDB";                // same, CP949 bytes
const std::string kChiUtf8 = "\xEC\xB9\x98";                         // U+CE58 "chi"
const std::string kReplacement = "\xEF\xBF\xBD";                     // U+FFFD

} // namespace

TEST_CASE("CP949 table covers the documented rectangle and known vectors") {
    std::size_t mapped = 0;
    std::size_t surrogates = 0;
    for (std::size_t index = 0; index < renderojn::text::kCp949LeadCount * renderojn::text::kCp949TrailCount; ++index) {
        const auto value = renderojn::text::detail::kCp949Table[index];
        if (value != 0) ++mapped;
        if (value >= 0xD800 && value <= 0xDFFF) ++surrogates;
    }
    CHECK(mapped == 17048);
    CHECK(surrogates == 0);

    CHECK(renderojn::text::cp949_to_codepoint(0xB0, 0xA1) == char32_t{0xAC00});  // U+AC00
    CHECK(renderojn::text::cp949_to_codepoint(0xC6, 0x52) == char32_t{0xD7A3});  // U+D7A3
    CHECK(renderojn::text::cp949_to_codepoint(0x8C, 0x63) == char32_t{0xB620});  // U+B620 (UHC extension)
    CHECK(renderojn::text::cp949_to_codepoint(0xA1, 0xA4) == char32_t{0x00B7});  // middle dot
    CHECK(renderojn::text::cp949_to_codepoint(0xA2, 0xE6) == char32_t{0x20AC});  // euro sign
    CHECK(renderojn::text::cp949_to_codepoint(0xAA, 0xA2) == char32_t{0x3042});  // hiragana a
    CHECK(renderojn::text::cp949_to_codepoint(0xC9, 0xA1) == 0);          // user-defined row
    CHECK(renderojn::text::cp949_to_codepoint(0x80, 0x41) == 0);          // lead below range
    CHECK(renderojn::text::cp949_to_codepoint(0xB0, 0x40) == 0);          // trail below range
    CHECK(renderojn::text::cp949_to_codepoint(0xB0, 0xFF) == 0);          // trail above range
}

TEST_CASE("CP949 decoding produces UTF-8 and marks undecodable bytes") {
    bool lossy = true;
    CHECK(renderojn::text::decode_cp949(kHangeulCp949, &lossy) == kHangeulUtf8);
    CHECK_FALSE(lossy);

    CHECK(renderojn::text::decode_cp949("A" + kHangeulCp949 + "B", &lossy) == "A" + kHangeulUtf8 + "B");
    CHECK_FALSE(lossy);

    // A dangling lead byte becomes U+FFFD and consumes only itself.
    CHECK(renderojn::text::decode_cp949("\xB0", &lossy) == kReplacement);
    CHECK(lossy);
    CHECK(renderojn::text::decode_cp949("\xB0 ", &lossy) == kReplacement + " ");
    CHECK(lossy);
    CHECK(renderojn::text::decode_cp949("", &lossy).empty());
    CHECK_FALSE(lossy);
}

TEST_CASE("UTF-8 validation is strict and reports wide sequences") {
    CHECK(renderojn::text::check_utf8("plain").valid);
    CHECK_FALSE(renderojn::text::check_utf8("plain").has_wide_sequence);
    CHECK(renderojn::text::check_utf8(kHangeulUtf8).valid);
    CHECK(renderojn::text::check_utf8(kHangeulUtf8).has_wide_sequence);
    CHECK(renderojn::text::check_utf8("\xC4\xA1").valid);            // U+0121, two bytes
    CHECK_FALSE(renderojn::text::check_utf8("\xC4\xA1").has_wide_sequence);
    CHECK_FALSE(renderojn::text::check_utf8("\xC0\x80").valid);      // overlong NUL
    CHECK_FALSE(renderojn::text::check_utf8("\xED\xA0\x80").valid);  // surrogate
    CHECK_FALSE(renderojn::text::check_utf8("\xF4\x90\x80\x80").valid);  // above U+10FFFF
    CHECK_FALSE(renderojn::text::check_utf8("\xE0\x80").valid);      // truncated
    CHECK_FALSE(renderojn::text::check_utf8("\xC7\xD1\xB1\xDB").valid);  // CP949 is not UTF-8
}

TEST_CASE("OJN text policy prefers the encoding the game wrote") {
    CHECK(renderojn::text::decode_ojn_text("Ruthless") == "Ruthless");
    CHECK(renderojn::text::decode_ojn_text("").empty());
    // CP949 in, UTF-8 out.
    CHECK(renderojn::text::decode_ojn_text(kHangeulCp949) == kHangeulUtf8);
    // Already-UTF-8 text is left alone.
    CHECK(renderojn::text::decode_ojn_text(kHangeulUtf8) == kHangeulUtf8);
    // Valid as both: a two-byte-only sequence is CP949 (U+CE58), not U+0121.
    CHECK(renderojn::text::decode_ojn_text("\xC4\xA1") == kChiUtf8);
    // The reporter's title, byte for byte.
    const std::string reported = "\xC0\xAF\xB7\xC9\xC0\xC7 \xC3\xE0\xC1\xA6" "2(Sneak)";
    CHECK(renderojn::text::decode_ojn_text(reported) ==
          "\xEC\x9C\xA0\xEB\xA0\xB9\xEC\x9D\x98 \xEC\xB6\x95\xEC\xA0\x9C" "2(Sneak)");
}

TEST_CASE("OJN text policy falls back to Latin-1 rather than emitting U+FFFD") {
    // "Cafe" + 0xE9 (Latin-1 e-acute): not valid UTF-8, and 0xE9 followed by an
    // ASCII byte is not a CP949 pair, so it widens as Latin-1 -- byte for byte
    // what RenderOJN <= 1.0.2 tagged, never U+FFFD.  0xC3 0xA9 is U+00E9 in UTF-8.
    CHECK(renderojn::text::decode_ojn_text("Caf\xE9") == "Caf\xC3\xA9");
    // "F" + 0xFC (Latin-1 u-umlaut) + "r Elise"; 0xC3 0xBC is U+00FC.
    CHECK(renderojn::text::decode_ojn_text("F\xFCr Elise") == "F\xC3\xBCr Elise");
    // A lone high byte is Latin-1 (U+00B0), not the replacement character.
    CHECK(renderojn::text::decode_ojn_text("\xB0") == "\xC2\xB0");
}

TEST_CASE("decode_latin1 widens every byte losslessly") {
    CHECK(renderojn::text::decode_latin1("A\xFF\x80") == "A\xC3\xBF\xC2\x80");
    CHECK(renderojn::text::decode_latin1("").empty());
}
