#pragma once

#include "core/io/ByteReader.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace renderojn::test_fixture {

inline void u16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value)); bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
}
inline void u32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) bytes.push_back(static_cast<std::uint8_t>(value >> shift));
}
inline void f32(std::vector<std::uint8_t>& bytes, float value) {
    std::uint32_t bits{}; std::memcpy(&bits, &value, sizeof(bits)); u32(bytes, bits);
}
inline void fixed(std::vector<std::uint8_t>& bytes, const std::string& value, std::size_t size) {
    bytes.insert(bytes.end(), value.begin(), value.end()); bytes.resize(bytes.size() + size - value.size(), 0);
}

struct OjnEventSet {
    std::uint32_t measure{};
    std::uint16_t channel{};
    std::uint16_t slot_count{};
    std::vector<std::array<std::uint8_t, 4>> records;
};

struct OjnChartCounts {
    std::uint32_t event_count{};
    std::uint32_t note_count{};
    std::uint32_t measure_count{};
    std::uint32_t package_count{};
};

struct OrdinaryOjnSpec {
    float tempo{120.0F};
    std::array<OjnChartCounts, 3> counts{};
    std::array<std::uint32_t, 3> durations{{1, 1, 1}};
    std::vector<OjnEventSet> hard_packages;
};

inline std::array<std::uint8_t, 4> ojn_record(std::uint16_t value, std::uint8_t volume = 0, std::uint8_t type = 0) {
    return {{static_cast<std::uint8_t>(value), static_cast<std::uint8_t>(value >> 8U), volume, type}};
}

inline std::array<std::uint8_t, 4> ojn_scalar_record(float value) {
    std::uint32_t bits{};
    std::memcpy(&bits, &value, sizeof(bits));
    return {{static_cast<std::uint8_t>(bits), static_cast<std::uint8_t>(bits >> 8U),
             static_cast<std::uint8_t>(bits >> 16U), static_cast<std::uint8_t>(bits >> 24U)}};
}

inline std::vector<std::uint8_t> ogg_signature() {
    return {static_cast<std::uint8_t>('O'), static_cast<std::uint8_t>('g'), static_cast<std::uint8_t>('g'), static_cast<std::uint8_t>('S')};
}

inline std::uint32_t ogg_test_crc(const std::vector<std::uint8_t>& page) {
    std::uint32_t crc{};
    for (std::size_t index = 0; index < page.size(); ++index) {
        const auto byte = index >= 22U && index < 26U ? 0U : page[index];
        crc ^= static_cast<std::uint32_t>(byte) << 24U;
        for (unsigned int bit = 0; bit < 8U; ++bit) {
            crc = (crc & 0x80000000U) != 0U ? (crc << 1U) ^ 0x04c11db7U : crc << 1U;
        }
    }
    return crc;
}

inline std::vector<std::uint8_t> ogg_page(std::uint8_t flags, std::uint32_t serial, std::uint32_t sequence,
                                          const std::vector<std::uint8_t>& lacing, const std::vector<std::uint8_t>& body) {
    if (lacing.size() > 255U) throw std::logic_error("Ogg fixture has too many lacing values");
    std::size_t body_size{};
    for (const auto value : lacing) body_size += value;
    if (body_size != body.size()) throw std::logic_error("Ogg fixture lacing does not match body length");

    std::vector<std::uint8_t> page{'O', 'g', 'g', 'S', 0, flags};
    page.insert(page.end(), 8, 0);
    u32(page, serial); u32(page, sequence); u32(page, 0);
    page.push_back(static_cast<std::uint8_t>(lacing.size()));
    page.insert(page.end(), lacing.begin(), lacing.end());
    page.insert(page.end(), body.begin(), body.end());
    const auto crc = ogg_test_crc(page);
    for (int shift = 0; shift < 32; shift += 8) page[22U + static_cast<std::size_t>(shift / 8)] = static_cast<std::uint8_t>(crc >> shift);
    return page;
}

inline std::vector<std::uint8_t> ogg_vorbis_stream(bool multi_page) {
    const std::vector<std::uint8_t> identification{1, 'v', 'o', 'r', 'b', 'i', 's'};
    const std::vector<std::uint8_t> comment{3, 'v', 'o', 'r', 'b', 'i', 's'};
    const std::vector<std::uint8_t> setup{5, 'v', 'o', 'r', 'b', 'i', 's'};
    std::vector<std::uint8_t> stream;
    const auto append = [&stream](const std::vector<std::uint8_t>& page) { stream.insert(stream.end(), page.begin(), page.end()); };
    if (!multi_page) {
        std::vector<std::uint8_t> headers;
        headers.insert(headers.end(), identification.begin(), identification.end());
        headers.insert(headers.end(), comment.begin(), comment.end());
        headers.insert(headers.end(), setup.begin(), setup.end());
        append(ogg_page(0x06U, 0x12345678U, 0U, {7, 7, 7}, headers));
        return stream;
    }
    append(ogg_page(0x02U, 0x12345678U, 0U, {7}, identification));
    append(ogg_page(0x00U, 0x12345678U, 1U, {7}, comment));
    append(ogg_page(0x00U, 0x12345678U, 2U, {7}, setup));
    append(ogg_page(0x00U, 0x12345678U, 3U, {1}, {0}));
    append(ogg_page(0x04U, 0x12345678U, 4U, {1}, {0}));
    return stream;
}

inline std::shared_ptr<const io::ByteBuffer> ordinary_ojn(const OrdinaryOjnSpec& spec) {
    std::vector<std::uint8_t> bytes;
    u32(bytes, 42); u32(bytes, 0x006e6a6fU); f32(bytes, 0.0F); u32(bytes, 2); f32(bytes, spec.tempo);
    u16(bytes, 1); u16(bytes, 2); u16(bytes, 3); u16(bytes, 0);
    for (const auto& counts : spec.counts) u32(bytes, counts.event_count);
    for (const auto& counts : spec.counts) u32(bytes, counts.note_count);
    for (const auto& counts : spec.counts) u32(bytes, counts.measure_count);
    for (const auto& counts : spec.counts) u32(bytes, counts.package_count);
    u16(bytes, 0); u16(bytes, 0); fixed(bytes, "", 20); u32(bytes, 0); f32(bytes, 1.0F);
    fixed(bytes, "Synthetic Title", 64); fixed(bytes, "Synthetic Artist", 32); fixed(bytes, "Fixture", 32); fixed(bytes, "synthetic.ojm", 32);
    u32(bytes, 0); for (const auto duration : spec.durations) u32(bytes, duration);
    u32(bytes, 300); u32(bytes, 300); u32(bytes, 300);
    const auto chart_end_offset = [&]() {
        std::size_t size = 300;
        for (const auto& package : spec.hard_packages) size += 8 + package.records.size() * 4;
        return static_cast<std::uint32_t>(size);
    }();
    u32(bytes, chart_end_offset);
    for (const auto& package : spec.hard_packages) {
        if (package.slot_count != package.records.size()) throw std::logic_error("OJN fixture slot count does not match records");
        u32(bytes, package.measure); u16(bytes, package.channel); u16(bytes, package.slot_count);
        for (const auto& record : package.records) bytes.insert(bytes.end(), record.begin(), record.end());
    }
    return std::make_shared<io::ByteBuffer>(std::move(bytes));
}

inline std::shared_ptr<const io::ByteBuffer> ordinary_ojn() {
    OrdinaryOjnSpec spec;
    spec.counts[2] = {1, 1, 0, 1};
    spec.hard_packages = {{0, 2, 1, {ojn_record(1)}}};
    return ordinary_ojn(spec);
}

// Wraps an ordinary OJN in the Korea-era `new` container.  This is the inverse
// of the parser's decryption, so a fixture built here and then parsed exercises
// a genuine round trip rather than a restatement of the implementation: the
// payload is XORed against a blockSize-long key and written back to front,
// behind an 8-byte header carrying the key parameters.
inline std::shared_ptr<const io::ByteBuffer> new_wrapped_ojn(std::shared_ptr<const io::ByteBuffer> plain,
                                                             std::uint8_t block_size = 11,
                                                             std::uint8_t main_key = 0x46U,
                                                             std::uint8_t mid_key = 0xe1U,
                                                             std::uint8_t initial_key = 0x85U) {
    const auto& source = plain->bytes();
    std::vector<std::uint8_t> key(block_size == 0 ? std::size_t{1} : block_size, main_key);
    if (block_size != 0) {
        key[0] = initial_key;
        key[static_cast<std::size_t>(block_size) / 2U] = mid_key;
    }
    // Byte 7 is unused by the format -- real files carry arbitrary values there
    // and both this fixture and the parser ignore it, matching CXO2.
    std::vector<std::uint8_t> bytes(source.size() + 8U);
    bytes[0] = 'n'; bytes[1] = 'e'; bytes[2] = 'w';
    bytes[3] = block_size; bytes[4] = main_key; bytes[5] = mid_key; bytes[6] = initial_key;
    // The payload occupies bytes [8, size) and is read backwards from the very
    // end during decryption, so emit it reversed here.
    for (std::size_t offset = 0; offset < source.size(); ++offset) {
        const auto encrypted = static_cast<std::uint8_t>(source[offset] ^ key[block_size == 0 ? 0U : offset % block_size]);
        bytes[(bytes.size() - 1U) - offset] = encrypted;
    }
    return std::make_shared<io::ByteBuffer>(std::move(bytes));
}

inline std::vector<std::uint8_t> riff_bytes() {
    return {static_cast<std::uint8_t>('r'), static_cast<std::uint8_t>('i'), static_cast<std::uint8_t>('f'), static_cast<std::uint8_t>('f')};
}

// M30 samples always name Ogg payloads, so decoded bytes must begin with
// `OggS` on every supported flag.
inline std::vector<std::uint8_t> ogg_payload() { return {'O', 'g', 'g', 'S'}; }

// Builds an M30 package for an arbitrary encryption flag.  Only flag 16 applies
// the `nami` transform; flags 0 and 32 store payload bytes verbatim, matching
// CXO2's behavior of transforming only flag 16.
inline std::shared_ptr<const io::ByteBuffer> m30_package(std::uint32_t flag,
                                                         std::vector<std::uint8_t> decoded = ogg_payload(),
                                                         std::uint16_t codec_code = 5, std::uint16_t reference = 0) {
    std::vector<std::uint8_t> bytes{'M', '3', '0', 0};
    u32(bytes, 1); u32(bytes, flag); u32(bytes, 1); u32(bytes, 28); u32(bytes, static_cast<std::uint32_t>(52 + decoded.size())); u32(bytes, 0);
    fixed(bytes, "sample", 32); u32(bytes, static_cast<std::uint32_t>(decoded.size())); u16(bytes, codec_code); bytes.insert(bytes.end(), 6, 0);
    u16(bytes, reference); bytes.insert(bytes.end(), 6, 0);
    if (flag == 16U) {
        for (std::size_t index = 0; index + 3 < decoded.size(); index += 4) {
            decoded[index] ^= 'n'; decoded[index + 1] ^= 'a'; decoded[index + 2] ^= 'm'; decoded[index + 3] ^= 'i';
        }
    }
    bytes.insert(bytes.end(), decoded.begin(), decoded.end());
    return std::make_shared<io::ByteBuffer>(std::move(bytes));
}

inline std::shared_ptr<const io::ByteBuffer> m30_flag16(std::vector<std::uint8_t> decoded = ogg_payload(),
                                                        std::uint16_t codec_code = 5, std::uint16_t reference = 0) {
    return m30_package(16U, std::move(decoded), codec_code, reference);
}

inline std::shared_ptr<const io::ByteBuffer> omc_or_ojm_with_slots(const std::string& signature,
                                                                    const std::vector<std::vector<std::uint8_t>>& pcm_records,
                                                                    const std::vector<std::vector<std::uint8_t>>& ogg_records,
                                                                    std::size_t trailing_empty_ogg_records = 0) {
    if (signature != "OMC" && signature != "OJM") throw std::logic_error("fixture signature must be OMC or OJM");
    if (pcm_records.size() > std::numeric_limits<std::uint16_t>::max() || ogg_records.size() > std::numeric_limits<std::uint16_t>::max()) {
        throw std::logic_error("fixture directory count exceeds u16");
    }
    std::size_t pcm_size{};
    for (const auto& record : pcm_records) pcm_size += 56 + record.size();
    const auto ogg_offset = static_cast<std::uint32_t>(20 + pcm_size);

    std::vector<std::uint8_t> bytes{static_cast<std::uint8_t>(signature[0]), static_cast<std::uint8_t>(signature[1]),
                                    static_cast<std::uint8_t>(signature[2]), 0};
    u16(bytes, static_cast<std::uint16_t>(pcm_records.size())); u16(bytes, static_cast<std::uint16_t>(ogg_records.size()));
    u32(bytes, 20); u32(bytes, ogg_offset); u32(bytes, 0);
    for (std::size_t index = 0; index < pcm_records.size(); ++index) {
        const auto& record = pcm_records[index];
        fixed(bytes, "pcm" + std::to_string(index), 32); u16(bytes, 1); u16(bytes, 1); u32(bytes, 48000); u32(bytes, 96000); u16(bytes, 2); u16(bytes, 16);
        u32(bytes, 0); u32(bytes, static_cast<std::uint32_t>(record.size()));
        bytes.insert(bytes.end(), record.begin(), record.end());
    }
    for (std::size_t index = 0; index < ogg_records.size(); ++index) {
        const auto& record = ogg_records[index];
        fixed(bytes, "ogg" + std::to_string(index), 32); u32(bytes, static_cast<std::uint32_t>(record.size()));
        bytes.insert(bytes.end(), record.begin(), record.end());
    }
    for (std::size_t index = 0; index < trailing_empty_ogg_records; ++index) {
        fixed(bytes, "trailing" + std::to_string(index), 32); u32(bytes, 0);
    }
    const auto size = static_cast<std::uint32_t>(bytes.size());
    for (int shift = 0; shift < 32; shift += 8) bytes[16 + shift / 8] = static_cast<std::uint8_t>(size >> shift);
    return std::make_shared<io::ByteBuffer>(std::move(bytes));
}

inline std::shared_ptr<const io::ByteBuffer> ojm_with_empty_ogg_sentinel() {
    return omc_or_ojm_with_slots("OJM", {}, {ogg_signature()}, 1);
}

inline std::shared_ptr<const io::ByteBuffer> omc_with_shuffled_pcm() {
    std::vector<std::uint8_t> payload;
    for (std::uint8_t value = 0; value < 17; ++value) payload.push_back(value);
    return omc_or_ojm_with_slots("OMC", {payload}, {});
}

inline std::shared_ptr<const io::ByteBuffer> ojm_with_plain_pcm() {
    std::vector<std::uint8_t> payload;
    for (std::uint8_t value = 0; value < 17; ++value) payload.push_back(value);
    return omc_or_ojm_with_slots("OJM", {payload}, {});
}

} // namespace renderojn::test_fixture
