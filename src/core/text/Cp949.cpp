#include "core/text/Cp949.hpp"

namespace renderojn::text {

namespace {

constexpr char32_t kReplacement = 0xFFFD;

} // namespace

char32_t cp949_to_codepoint(std::uint8_t lead, std::uint8_t trail) noexcept {
    if (lead < 0x81 || lead > 0xFE || trail < 0x41 || trail > 0xFE) return 0;
    const auto index = static_cast<std::size_t>(lead - 0x81) * kCp949TrailCount + static_cast<std::size_t>(trail - 0x41);
    return detail::kCp949Table[index];
}

void append_utf8(std::string& out, char32_t code_point) {
    if (code_point < 0x80) {
        out.push_back(static_cast<char>(code_point));
    } else if (code_point < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else if (code_point < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    }
}

Utf8Check check_utf8(std::string_view bytes) noexcept {
    Utf8Check result{true, false};
    std::size_t index = 0;
    while (index < bytes.size()) {
        const auto lead = static_cast<std::uint8_t>(bytes[index]);
        std::size_t length{};
        char32_t code_point{};
        char32_t minimum{};
        if (lead < 0x80) {
            ++index;
            continue;
        } else if ((lead & 0xE0) == 0xC0) {
            length = 2; code_point = lead & 0x1F; minimum = 0x80;
        } else if ((lead & 0xF0) == 0xE0) {
            length = 3; code_point = lead & 0x0F; minimum = 0x800;
        } else if ((lead & 0xF8) == 0xF0) {
            length = 4; code_point = lead & 0x07; minimum = 0x10000;
        } else {
            return {false, false};
        }
        if (index + length > bytes.size()) return {false, false};
        for (std::size_t offset = 1; offset < length; ++offset) {
            const auto continuation = static_cast<std::uint8_t>(bytes[index + offset]);
            if ((continuation & 0xC0) != 0x80) return {false, false};
            code_point = (code_point << 6) | (continuation & 0x3F);
        }
        if (code_point < minimum || code_point > 0x10FFFF || (code_point >= 0xD800 && code_point <= 0xDFFF)) {
            return {false, false};
        }
        if (length >= 3) result.has_wide_sequence = true;
        index += length;
    }
    return result;
}

std::string decode_cp949(std::string_view bytes, bool* lossy) {
    std::string out;
    out.reserve(bytes.size() * 3 / 2);
    bool lost = false;
    std::size_t index = 0;
    while (index < bytes.size()) {
        const auto lead = static_cast<std::uint8_t>(bytes[index]);
        if (lead < 0x80) {
            out.push_back(static_cast<char>(lead));
            ++index;
            continue;
        }
        char32_t code_point{};
        if (index + 1 < bytes.size()) {
            code_point = cp949_to_codepoint(lead, static_cast<std::uint8_t>(bytes[index + 1]));
        }
        if (code_point != 0) {
            append_utf8(out, code_point);
            index += 2;
        } else {
            // A lead with no valid trail (or a dangling lead at the end)
            // consumes only itself, so the following byte gets its own chance.
            append_utf8(out, kReplacement);
            lost = true;
            ++index;
        }
    }
    if (lossy != nullptr) *lossy = lost;
    return out;
}

std::string decode_latin1(std::string_view bytes) {
    std::string out;
    out.reserve(bytes.size());
    for (const auto byte : bytes) {
        append_utf8(out, static_cast<char32_t>(static_cast<std::uint8_t>(byte)));
    }
    return out;
}

std::string decode_ojn_text(std::string_view bytes) {
    bool ascii = true;
    for (const auto byte : bytes) {
        if (static_cast<std::uint8_t>(byte) >= 0x80) {
            ascii = false;
            break;
        }
    }
    if (ascii) return std::string(bytes);

    // Real UTF-8 Korean/kana/kanji always carries a 3- or 4-byte sequence, so a
    // valid-and-wide UTF-8 string is taken as-is before CP949 is even tried.
    const auto utf8 = check_utf8(bytes);
    if (utf8.valid && utf8.has_wide_sequence) return std::string(bytes);

    // CP949 is what the game wrote, so a clean CP949 decode wins next -- this is
    // where an ambiguous 2-byte-only string (valid as both encodings) lands.
    bool lossy = false;
    auto decoded = decode_cp949(bytes, &lossy);
    if (!lossy) return decoded;

    // Neither a wide UTF-8 string nor clean CP949: keep whatever valid UTF-8 we
    // were handed, otherwise widen as Latin-1.  Latin-1 is byte-preserving and
    // matches RenderOJN <= 1.0.2, so a Western title never becomes U+FFFD.
    if (utf8.valid) return std::string(bytes);
    return decode_latin1(bytes);
}

} // namespace renderojn::text
