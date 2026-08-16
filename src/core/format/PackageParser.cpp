#include "core/format/PackageParser.hpp"

#include "core/Diagnostic.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <set>

namespace renderojn::format {
namespace {

constexpr std::size_t kMaxEncodedSampleBytes = 256U * 1024U * 1024U;

[[noreturn]] void malformed(const std::string& message) {
    throw Error(ExitCode::Runtime, "Malformed sample package: " + message);
}

void append_sample(Package& package, std::uint16_t id, std::vector<std::uint8_t> bytes, std::set<std::uint16_t>& ids) {
    if (bytes.empty()) malformed("encoded sample is empty");
    if (bytes.size() > kMaxEncodedSampleBytes) malformed("encoded sample exceeds 256 MiB limit");
    if (package.samples.size() >= kMaxSamples) malformed("sample count exceeds limit");
    if (!ids.insert(id).second) malformed("duplicate sample id");
    package.samples.push_back({id, std::move(bytes)});
}

constexpr std::array<std::uint8_t, 290> kOmCRearrangeTable = {
0x10, 0x0E, 0x02, 0x09, 0x04, 0x00, 0x07, 0x01,
0x06, 0x08, 0x0F, 0x0A, 0x05, 0x0C, 0x03, 0x0D,
0x0B, 0x07, 0x02, 0x0A, 0x0B, 0x03, 0x05, 0x0D,
0x08, 0x04, 0x00, 0x0C, 0x06, 0x0F, 0x0E, 0x10,
0x01, 0x09, 0x0C, 0x0D, 0x03, 0x00, 0x06, 0x09,
0x0A, 0x01, 0x07, 0x08, 0x10, 0x02, 0x0B, 0x0E,
0x04, 0x0F, 0x05, 0x08, 0x03, 0x04, 0x0D, 0x06,
0x05, 0x0B, 0x10, 0x02, 0x0C, 0x07, 0x09, 0x0A,
0x0F, 0x0E, 0x00, 0x01, 0x0F, 0x02, 0x0C, 0x0D,
0x00, 0x04, 0x01, 0x05, 0x07, 0x03, 0x09, 0x10,
0x06, 0x0B, 0x0A, 0x08, 0x0E, 0x00, 0x04, 0x0B,
0x10, 0x0F, 0x0D, 0x0C, 0x06, 0x05, 0x07, 0x01,
0x02, 0x03, 0x08, 0x09, 0x0A, 0x0E, 0x03, 0x10,
0x08, 0x07, 0x06, 0x09, 0x0E, 0x0D, 0x00, 0x0A,
0x0B, 0x04, 0x05, 0x0C, 0x02, 0x01, 0x0F, 0x04,
0x0E, 0x10, 0x0F, 0x05, 0x08, 0x07, 0x0B, 0x00,
0x01, 0x06, 0x02, 0x0C, 0x09, 0x03, 0x0A, 0x0D,
0x06, 0x0D, 0x0E, 0x07, 0x10, 0x0A, 0x0B, 0x00,
0x01, 0x0C, 0x0F, 0x02, 0x03, 0x08, 0x09, 0x04,
0x05, 0x0A, 0x0C, 0x00, 0x08, 0x09, 0x0D, 0x03,
0x04, 0x05, 0x10, 0x0E, 0x0F, 0x01, 0x02, 0x0B,
0x06, 0x07, 0x05, 0x06, 0x0C, 0x04, 0x0D, 0x0F,
0x07, 0x0E, 0x08, 0x01, 0x09, 0x02, 0x10, 0x0A,
0x0B, 0x00, 0x03, 0x0B, 0x0F, 0x04, 0x0E, 0x03,
0x01, 0x00, 0x02, 0x0D, 0x0C, 0x06, 0x07, 0x05,
0x10, 0x09, 0x08, 0x0A, 0x03, 0x02, 0x01, 0x00,
0x04, 0x0C, 0x0D, 0x0B, 0x10, 0x05, 0x06, 0x0F,
0x0E, 0x07, 0x09, 0x0A, 0x08, 0x09, 0x0A, 0x00,
0x07, 0x08, 0x06, 0x10, 0x03, 0x04, 0x01, 0x02,
0x05, 0x0B, 0x0E, 0x0F, 0x0D, 0x0C, 0x0A, 0x06,
0x09, 0x0C, 0x0B, 0x10, 0x07, 0x08, 0x00, 0x0F,
0x03, 0x01, 0x02, 0x05, 0x0D, 0x0E, 0x04, 0x0D,
0x00, 0x01, 0x0E, 0x02, 0x03, 0x08, 0x0B, 0x07,
0x0C, 0x09, 0x05, 0x0A, 0x0F, 0x04, 0x06, 0x10,
0x01, 0x0E, 0x02, 0x03, 0x0D, 0x0B, 0x07, 0x00,
0x08, 0x0C, 0x09, 0x06, 0x0F, 0x10, 0x05, 0x0A,
0x04, 0x00};

struct OmcXorState {
    std::uint8_t key_byte{0xffU};
    int key_counter{};
};

std::vector<std::uint8_t> make_pcm_wave(std::vector<std::uint8_t> pcm_bytes,
                                        std::uint16_t audio_format, std::uint16_t channels,
                                        std::uint32_t sample_rate, std::uint32_t byte_rate,
                                        std::uint16_t block_align, std::uint16_t bits_per_sample) {
    if (pcm_bytes.empty()) return {};
    std::vector<std::uint8_t> wav;
    const auto data_size = static_cast<std::uint32_t>(pcm_bytes.size());
    const auto riff_size = data_size + 36U;
    wav.reserve(static_cast<std::size_t>(data_size) + 44U);
    const auto put_u16 = [&wav](std::uint16_t value) { wav.push_back(static_cast<std::uint8_t>(value)); wav.push_back(static_cast<std::uint8_t>(value >> 8U)); };
    const auto put_u32 = [&wav](std::uint32_t value) { for (int shift = 0; shift < 32; shift += 8) wav.push_back(static_cast<std::uint8_t>(value >> shift)); };
    wav.insert(wav.end(), {'R', 'I', 'F', 'F'}); put_u32(riff_size); wav.insert(wav.end(), {'W', 'A', 'V', 'E'});
    wav.insert(wav.end(), {'f', 'm', 't', ' '}); put_u32(16); put_u16(audio_format); put_u16(channels); put_u32(sample_rate);
    put_u32(byte_rate); put_u16(block_align); put_u16(bits_per_sample); wav.insert(wav.end(), {'d', 'a', 't', 'a'}); put_u32(data_size);
    wav.insert(wav.end(), pcm_bytes.begin(), pcm_bytes.end());
    return wav;
}

std::vector<std::uint8_t> decode_omc_wave(std::vector<std::uint8_t> encoded, OmcXorState& xor_state,
                                          std::uint16_t audio_format, std::uint16_t channels,
                                          std::uint32_t sample_rate, std::uint32_t byte_rate, std::uint16_t block_align,
                                          std::uint16_t bits_per_sample) {
    // OMC PCM payloads are rearranged in 17 blocks, then ACCXOR decoded.  This is
    // intentionally kept separate from M30 nami decoding; the package kinds have
    // distinct record and transform rules.
    if (encoded.empty()) return {};
    std::vector<std::uint8_t> decoded(encoded.size());
    const auto block_size = encoded.size() / 17U;
    const auto key = ((encoded.size() % 17U) << 4U) + (encoded.size() % 17U);
    if (key >= kOmCRearrangeTable.size()) malformed("invalid OMC rearrange key");
    for (std::size_t block = 0; block < 17U; ++block) {
        // The encoded stream stores its blocks sequentially.  The table tells
        // us where each of those blocks belongs in the plaintext stream.
        const auto source_offset = block_size * block;
        const auto destination_offset = block_size * kOmCRearrangeTable[key + block];
        if (source_offset + block_size > encoded.size() || destination_offset + block_size > decoded.size()) {
            malformed("invalid OMC rearrange range");
        }
        std::memcpy(decoded.data() + destination_offset, encoded.data() + source_offset, block_size);
    }
    // The division can leave a tail shorter than one block.  It was not permuted.
    const auto transformed_size = block_size * 17U;
    std::copy(encoded.begin() + static_cast<std::ptrdiff_t>(transformed_size), encoded.end(),
              decoded.begin() + static_cast<std::ptrdiff_t>(transformed_size));

    for (auto& byte : decoded) {
        const auto prior = byte;
        if (((static_cast<unsigned int>(xor_state.key_byte) << xor_state.key_counter) & 0x80U) != 0U) byte = static_cast<std::uint8_t>(~byte);
        ++xor_state.key_counter;
        if (xor_state.key_counter > 7) {
            xor_state.key_counter = 0;
            xor_state.key_byte = prior;
        }
    }

    return make_pcm_wave(std::move(decoded), audio_format, channels, sample_rate, byte_rate, block_align, bits_per_sample);
}

// M30 sample payloads are Ogg streams that are either stored verbatim or
// obfuscated with a repeating `nami` XOR.  CXO2 transforms only flag 16, and
// every flag-0 sample in the reference corpus already begins with a plain
// `OggS` signature, so flags 0 and 32 are plaintext variants.
enum class M30Encoding : std::uint8_t { Plain, Nami };

M30Encoding m30_encoding_for(std::uint32_t flag) {
    switch (flag) {
    case 0U:
    case 32U:
        return M30Encoding::Plain;
    case 16U:
        return M30Encoding::Nami;
    default:
        throw Error(ExitCode::Runtime,
                    "Unsupported sample-package encoding: M30 flag " + std::to_string(flag) +
                    " is not supported (supported flags are 0, 16/nami, and 32).");
    }
}

// `nami` obfuscation only covers complete four-byte groups.  A trailing partial
// group of 0-3 bytes is stored verbatim and must be left untouched.
void apply_nami(std::vector<std::uint8_t>& bytes) {
    constexpr std::array<std::uint8_t, 4> nami{{'n', 'a', 'm', 'i'}};
    for (std::size_t index = 0; index + 3 < bytes.size(); index += 4) {
        for (std::size_t key = 0; key < nami.size(); ++key) bytes[index + key] ^= nami[key];
    }
}

Package parse_m30(io::ByteReader input) {
    Package package{PackageKind::M30, {}};
    static_cast<void>(input.take(4, "M30 signature"));
    static_cast<void>(input.u32le("M30 version"));
    const auto flag = input.u32le("M30 encryption flag");
    const auto sample_count = input.u32le("M30 sample count");
    const auto data_offset = input.u32le("M30 data offset");
    const auto payload_size = input.u32le("M30 payload size");
    static_cast<void>(input.u32le("M30 padding"));
    if (sample_count > kMaxSamples) malformed("M30 sample count exceeds limit");
    if (data_offset < 28 || data_offset > input.end()) malformed("M30 data offset is outside package");
    if (payload_size != input.end() - data_offset) malformed("M30 payload size is inconsistent with package length");
    const auto encoding = m30_encoding_for(flag);
    input.seek(data_offset, "M30 data offset");
    std::set<std::uint16_t> ids;
    std::uint32_t records{};
    while (input.remaining() != 0) {
        if (++records > sample_count) malformed("M30 contains more records than declared");
        if (input.remaining() < 52) malformed("truncated M30 sample record");
        static_cast<void>(input.raw_fixed(32, "M30 sample name"));
        const auto size = input.u32le("M30 sample size");
        const auto codec_code = input.u16le("M30 sample codec code");
        static_cast<void>(input.take(6, "M30 sample reserved field"));
        const auto reference = input.u16le("M30 sample id");
        static_cast<void>(input.take(6, "M30 sample reserved field"));
        if (size > kMaxEncodedSampleBytes) malformed("M30 sample exceeds 256 MiB limit");
        if (codec_code != 0U && codec_code != 5U) {
            throw Error(ExitCode::Runtime,
                        "Unsupported sample-package encoding: M30 codec " + std::to_string(codec_code) +
                        " is not supported (supported codec codes are 0/background and 5/normal).");
        }
        auto bytes = input.take(size, "M30 sample payload");
        if (encoding == M30Encoding::Nami) apply_nami(bytes);
        // A decoded M30 sample must be a real Ogg stream.  Anything else means
        // the encoding was misidentified, so fail instead of handing an
        // unusable payload to the decoder.
        if (bytes.size() < 4 || std::memcmp(bytes.data(), "OggS", 4) != 0) {
            malformed("M30 sample payload is not an Ogg stream");
        }
        const auto adjusted = static_cast<std::uint32_t>(reference) + 1U + (codec_code == 0U ? 1000U : 0U);
        if (adjusted > std::numeric_limits<std::uint16_t>::max()) malformed("M30 sample id overflows");
        append_sample(package, static_cast<std::uint16_t>(adjusted), std::move(bytes), ids);
    }
    if (records != sample_count) malformed("M30 record count does not match header");
    return package;
}

Package parse_omc(io::ByteReader input, PackageKind kind) {
    Package package{kind, {}};
    static_cast<void>(input.take(4, "package signature"));
    const auto wav_count = input.u16le("PCM sample count");
    const auto ogg_count = input.u16le("Ogg sample count");
    const auto wav_offset = input.u32le("PCM data offset");
    const auto ogg_offset = input.u32le("Ogg data offset");
    const auto package_size = input.u32le("package file size");
    if (package_size != input.end()) malformed("package file size does not match input length");
    std::uint32_t directory_slots = static_cast<std::uint32_t>(wav_count) + static_cast<std::uint32_t>(ogg_count);
    if (directory_slots > kMaxSamples) malformed("sample-directory slot count exceeds limit");
    if (wav_offset < 20 || wav_offset > ogg_offset || ogg_offset > input.end()) malformed("invalid OMC/OJM offsets");
    std::set<std::uint16_t> ids;
    input.seek(wav_offset, "PCM data offset");
    OmcXorState xor_state;
    std::uint32_t id = 1;
    for (std::uint32_t index = 0; index < wav_count; ++index, ++id) {
        if (input.remaining() < 56 || input.position() + 56 > ogg_offset) malformed("truncated PCM sample record");
        static_cast<void>(input.raw_fixed(32, "PCM sample name"));
        const auto audio_format = input.u16le("PCM format");
        const auto channels = input.u16le("PCM channels");
        const auto sample_rate = input.u32le("PCM sample rate");
        const auto byte_rate = input.u32le("PCM byte rate");
        const auto block_align = input.u16le("PCM block alignment");
        const auto bits_per_sample = input.u16le("PCM bits per sample");
        static_cast<void>(input.u32le("PCM reserved field"));
        const auto size = input.u32le("PCM payload size");
        if (size > kMaxEncodedSampleBytes || size > ogg_offset - input.position()) malformed("invalid PCM payload size");
        if (size == 0) {
            ++package.empty_pcm_slot_records;
            continue;
        }
        auto bytes = input.take(size, "PCM payload");
        if (id > std::numeric_limits<std::uint16_t>::max()) malformed("PCM sample id overflows");
        auto wav = kind == PackageKind::OMC
                       ? decode_omc_wave(std::move(bytes), xor_state, audio_format, channels, sample_rate, byte_rate, block_align, bits_per_sample)
                       : make_pcm_wave(std::move(bytes), audio_format, channels, sample_rate, byte_rate, block_align, bits_per_sample);
        append_sample(package, static_cast<std::uint16_t>(id), std::move(wav), ids);
    }
    if (input.position() != ogg_offset) malformed("PCM records do not end at Ogg offset");
    id = 1001;
    for (std::uint32_t index = 0; index < ogg_count; ++index, ++id) {
        if (input.remaining() < 36) malformed("truncated Ogg sample record");
        static_cast<void>(input.raw_fixed(32, "Ogg sample name"));
        const auto size = input.u32le("Ogg payload size");
        if (size > kMaxEncodedSampleBytes) malformed("Ogg sample exceeds 256 MiB limit");
        if (size == 0) {
            ++package.empty_ogg_sentinel_records;
            continue;
        }
        auto bytes = input.take(size, "Ogg payload");
        if (id > std::numeric_limits<std::uint16_t>::max()) malformed("Ogg sample id overflows");
        append_sample(package, static_cast<std::uint16_t>(id), std::move(bytes), ids);
    }
    // O2Jam-authored OJM files can append zero-byte Ogg directory records after
    // the declared Ogg count. Accept only complete 36-byte records with no
    // payload; arbitrary, truncated, or payload-bearing tails remain malformed.
    while (input.remaining() != 0) {
        if (input.remaining() < 36) malformed("truncated trailing Ogg directory record");
        static_cast<void>(input.raw_fixed(32, "trailing Ogg sentinel name"));
        if (input.u32le("trailing Ogg sentinel payload size") != 0) {
            malformed("trailing Ogg record has an undeclared payload");
        }
        if (++directory_slots > kMaxSamples) malformed("sample-directory slot count exceeds limit");
        ++package.empty_ogg_sentinel_records;
    }
    return package;
}

} // namespace

Package parse_sample_package(const std::shared_ptr<const io::ByteBuffer>& buffer) {
    if (!buffer || buffer->size() < 4) malformed("truncated signature");
    io::ByteReader input(buffer);
    const auto magic = input.take(4, "signature");
    if (std::memcmp(magic.data(), "M30", 3) == 0) return parse_m30(io::ByteReader(buffer));
    if (std::memcmp(magic.data(), "OMC", 3) == 0) return parse_omc(io::ByteReader(buffer), PackageKind::OMC);
    if (std::memcmp(magic.data(), "OJM", 3) == 0) return parse_omc(io::ByteReader(buffer), PackageKind::OJM);
    throw Error(ExitCode::Runtime, "Unsupported sample package signature; expected M30, OMC, or OJM");
}

} // namespace renderojn::format
