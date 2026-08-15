#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// OJN header text (title, artist, charter, sample-package name) is stored as
// CP949 bytes: the Korean Windows code page, a superset of EUC-KR that also
// covers the kana and kanji Japanese-titled charts use.  Everything downstream
// (tag writers, the browser build, filenames) wants UTF-8, so the parser
// decodes here, with a table baked into the binary because the WebAssembly
// build has no iconv or ICU to lean on.
namespace renderojn::text {

constexpr std::size_t kCp949LeadCount = 0xFE - 0x81 + 1;   // lead bytes 0x81..0xFE
constexpr std::size_t kCp949TrailCount = 0xFE - 0x41 + 1;  // trail bytes 0x41..0xFE

namespace detail {
extern const std::uint16_t kCp949Table[kCp949LeadCount * kCp949TrailCount];
}

// Unicode code point of the CP949 sequence {lead, trail}, or 0 when the pair is
// outside the table or unmapped.
[[nodiscard]] char32_t cp949_to_codepoint(std::uint8_t lead, std::uint8_t trail) noexcept;

void append_utf8(std::string& out, char32_t code_point);

struct Utf8Check {
    bool valid{};              // well-formed: no overlongs, surrogates, or > U+10FFFF
    bool has_wide_sequence{};  // at least one 3- or 4-byte sequence
};
[[nodiscard]] Utf8Check check_utf8(std::string_view bytes) noexcept;

// Decodes CP949 to UTF-8.  Undecodable bytes become U+FFFD and set *lossy.
[[nodiscard]] std::string decode_cp949(std::string_view bytes, bool* lossy = nullptr);

// The policy for OJN header fields:
//   - pure ASCII is returned unchanged;
//   - text that is valid UTF-8 but not valid CP949 is already UTF-8;
//   - text that is valid CP949 but not UTF-8 is decoded;
//   - text valid as both is taken as UTF-8 only when it contains a 3- or 4-byte
//     sequence -- real UTF-8 Korean, kana or kanji always does, whereas CP949
//     can only masquerade as UTF-8 through 2-byte pairs (e.g. C4 A1, which is
//     "치" in CP949 and "ġ" in UTF-8) -- otherwise as CP949, the format's
//     native encoding;
//   - text valid as neither is decoded lossily as CP949.
// This is a heuristic; it is right for every chart in the corpus and errs
// toward the encoding the game itself wrote.
[[nodiscard]] std::string decode_ojn_text(std::string_view bytes);

} // namespace renderojn::text
