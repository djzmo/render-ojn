#include "core/audio/OggNormalizer.hpp"

#include "core/Diagnostic.hpp"
#include "core/io/ByteReader.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>

namespace renderojn::audio {
namespace {

constexpr std::size_t kPageHeaderBytes = 27U;
constexpr std::size_t kChecksumOffset = 22U;
constexpr std::uint8_t kContinuedPacketFlag = 0x01U;
constexpr std::uint8_t kBeginningOfStreamFlag = 0x02U;
constexpr std::uint8_t kEndOfStreamFlag = 0x04U;
constexpr std::uint8_t kKnownHeaderFlags = kContinuedPacketFlag | kBeginningOfStreamFlag | kEndOfStreamFlag;

[[noreturn]] void malformed(std::uint16_t sample_id, const std::string& message) {
    throw Error(ExitCode::Runtime, "Malformed Ogg/Vorbis sample " + std::to_string(sample_id) + ": " + message);
}

[[nodiscard]] std::uint32_t u32le(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
           (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

void put_u32le(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    for (std::size_t index = 0; index < 4U; ++index) bytes[offset + index] = static_cast<std::uint8_t>(value >> (index * 8U));
}

[[nodiscard]] const std::array<std::uint32_t, 256>& crc_table() {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> generated{};
        for (std::size_t index = 0; index < generated.size(); ++index) {
            auto value = static_cast<std::uint32_t>(index << 24U);
            for (unsigned int bit = 0; bit < 8U; ++bit) {
                value = (value & 0x80000000U) != 0U ? (value << 1U) ^ 0x04c11db7U : value << 1U;
            }
            generated[index] = value;
        }
        return generated;
    }();
    return table;
}

[[nodiscard]] std::uint32_t page_crc(const std::vector<std::uint8_t>& bytes, std::size_t page_offset, std::size_t page_size) {
    std::uint32_t crc{};
    const auto& table = crc_table();
    for (std::size_t index = 0; index < page_size; ++index) {
        const auto absolute = page_offset + index;
        const auto byte = index >= kChecksumOffset && index < kChecksumOffset + 4U ? 0U : bytes[absolute];
        crc = (crc << 8U) ^ table[((crc >> 24U) ^ byte) & 0xffU];
    }
    return crc;
}

struct VorbisHeaders {
    std::size_t completed{};
    std::array<std::uint8_t, 7> prefix{};
    std::size_t prefix_size{};
};

void add_segment(VorbisHeaders& headers, const std::uint8_t* data, std::size_t size, bool completes_packet, std::uint16_t sample_id) {
    if (headers.completed >= 3U) return;
    for (std::size_t index = 0; index < size && headers.prefix_size < headers.prefix.size(); ++index) {
        headers.prefix[headers.prefix_size++] = data[index];
    }
    if (!completes_packet) return;

    constexpr std::array<std::uint8_t, 3> kPacketTypes{{1U, 3U, 5U}};
    constexpr std::array<std::uint8_t, 6> kVorbis{{'v', 'o', 'r', 'b', 'i', 's'}};
    if (headers.prefix_size != headers.prefix.size() || headers.prefix[0] != kPacketTypes[headers.completed] ||
        !std::equal(kVorbis.begin(), kVorbis.end(), headers.prefix.begin() + 1)) {
        malformed(sample_id, "invalid Vorbis header packet");
    }
    ++headers.completed;
    headers.prefix_size = 0;
}

} // namespace

NormalizedOggPayload normalize_ogg_vorbis_pages(const format::EncodedSample& sample, Diagnostics& diagnostics) {
    const auto& input = sample.bytes;
    if (input.size() < kPageHeaderBytes) malformed(sample.id, "truncated initial page header");

    NormalizedOggPayload result{sample.id, &input, {}, 0};
    std::size_t offset{};
    std::uint32_t serial{};
    std::uint32_t previous_sequence{};
    bool previous_packet_continues{};
    bool saw_eos{};
    VorbisHeaders headers;

    while (offset < input.size()) {
        const auto remaining = input.size() - offset;
        if (remaining < kPageHeaderBytes) malformed(sample.id, "truncated page header");
        if (std::memcmp(input.data() + offset, "OggS", 4) != 0) malformed(sample.id, "missing page capture pattern");
        if (input[offset + 4U] != 0U) malformed(sample.id, "unsupported page version");

        const auto flags = input[offset + 5U];
        if ((flags & ~kKnownHeaderFlags) != 0U) malformed(sample.id, "unsupported page header flags");
        const auto segment_count = static_cast<std::size_t>(input[offset + 26U]);
        std::size_t header_size{};
        if (!io::checked_add(kPageHeaderBytes, segment_count, header_size) || header_size > remaining) {
            malformed(sample.id, "truncated page segment table");
        }
        std::size_t body_size{};
        for (std::size_t index = 0; index < segment_count; ++index) {
            if (!io::checked_add(body_size, static_cast<std::size_t>(input[offset + kPageHeaderBytes + index]), body_size)) {
                malformed(sample.id, "page body length overflows");
            }
        }
        std::size_t page_size{};
        if (!io::checked_add(header_size, body_size, page_size) || page_size > remaining) {
            malformed(sample.id, "truncated page body");
        }

        const auto page_serial = u32le(input, offset + 14U);
        const auto page_sequence = u32le(input, offset + 18U);
        const auto is_continued = (flags & kContinuedPacketFlag) != 0U;
        const auto is_bos = (flags & kBeginningOfStreamFlag) != 0U;
        const auto is_eos = (flags & kEndOfStreamFlag) != 0U;
        if (offset == 0U) {
            if (!is_bos || is_continued) malformed(sample.id, "first page must begin a complete logical stream");
            serial = page_serial;
        } else {
            if (saw_eos) malformed(sample.id, "page follows end-of-stream");
            if (is_bos) malformed(sample.id, "unexpected beginning-of-stream page");
            if (page_serial != serial) malformed(sample.id, "multiple logical stream serials are not supported");
            if (page_sequence != previous_sequence + 1U) malformed(sample.id, "page sequence is discontinuous");
            if (is_continued != previous_packet_continues) malformed(sample.id, "continued-packet flag is inconsistent");
        }

        const bool page_contains_vorbis_headers = headers.completed < 3U;
        std::size_t body_offset = offset + header_size;
        for (std::size_t index = 0; index < segment_count; ++index) {
            const auto segment_size = static_cast<std::size_t>(input[offset + kPageHeaderBytes + index]);
            add_segment(headers, input.data() + body_offset, segment_size, segment_size != 255U, sample.id);
            body_offset += segment_size;
        }
        const auto stored_crc = u32le(input, offset + kChecksumOffset);
        const auto calculated_crc = page_crc(input, offset, page_size);
        if (stored_crc != calculated_crc) {
            // Header pages establish the codec and stream identity. They are never
            // normalized; only later structurally valid data pages may be repaired.
            if (is_bos || page_contains_vorbis_headers) malformed(sample.id, "refusing to repair a Vorbis header page checksum");
            if (result.repaired_bytes.empty()) result.repaired_bytes = input;
            put_u32le(result.repaired_bytes, offset + kChecksumOffset, calculated_crc);
            ++result.repaired_page_count;
        }

        previous_sequence = page_sequence;
        previous_packet_continues = segment_count != 0U && input[offset + kPageHeaderBytes + segment_count - 1U] == 255U;
        if (is_eos) saw_eos = true;
        offset += page_size;
    }

    if (headers.completed != 3U) malformed(sample.id, "missing Vorbis header packets");
    if (!saw_eos) malformed(sample.id, "missing end-of-stream page");
    if (result.repaired_page_count != 0U) {
        diagnostics.warn("repaired " + std::to_string(result.repaired_page_count) + " legacy Ogg page checksum(s) for sample " +
                         std::to_string(sample.id));
    }
    return result;
}

} // namespace renderojn::audio
