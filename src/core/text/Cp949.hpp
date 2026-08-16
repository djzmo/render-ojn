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

// Widens each byte to the same-numbered code point (ISO-8859-1) and emits UTF-8.
// This never loses information: every byte 0x00-0xFF maps to exactly one code
// point, so it is the safe fallback for text that is neither valid UTF-8 nor
// cleanly decodable as CP949.
[[nodiscard]] std::string decode_latin1(std::string_view bytes);

// The policy for OJN header fields, in order:
//   - pure ASCII is returned unchanged;
//   - valid UTF-8 with a 3- or 4-byte sequence is already UTF-8 (real UTF-8
//     Korean, kana or kanji always has one) -- this wins over a coincidental
//     CP949 reading;
//   - otherwise, if the bytes decode cleanly as CP949, that decoding is used --
//     CP949 is the encoding the game wrote, so a 2-byte-only string that is
//     valid as both (e.g. C4 A1, "치" in CP949 and "ġ" in UTF-8) is read as
//     CP949;
//   - otherwise the bytes are widened as Latin-1, which is byte-preserving and
//     matches how RenderOJN <= 1.0.2 tagged Western titles; this catches
//     CP1252/ISO-8859-1 titles ("Café", "Für Elise") that CP949 cannot decode
//     without loss.
// This is a heuristic: it cannot tell a 2-byte-only UTF-8 title from a CP949
// one, and it reads other legacy CJK encodings (Shift_JIS, GBK, Big5) as CP949.
// It is right for every chart in the corpus and errs toward the game's own
// encoding, never toward U+FFFD.
[[nodiscard]] std::string decode_ojn_text(std::string_view bytes);

} // namespace renderojn::text
