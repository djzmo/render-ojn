#include "core/audio/Decoder.hpp"

#include "core/Diagnostic.hpp"
#include "core/audio/MemoryFile.hpp"
#include "core/audio/OggNormalizer.hpp"
#include "core/io/ByteReader.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <optional>
#include <string>

#ifdef RENDEROJN_EXTERNAL_DEPS
#include <sndfile.h>
#endif

namespace renderojn::audio {

namespace detail {

std::optional<std::int64_t> checked_memory_seek_position(std::int64_t base, std::int64_t offset,
                                                          std::int64_t length) noexcept {
    if (length < 0 || base < 0 || base > length) return std::nullopt;
    if (offset < 0 && offset < -base) return std::nullopt;
    if (offset > 0 && base > std::numeric_limits<std::int64_t>::max() - offset) return std::nullopt;
    const auto next = base + offset;
    if (next > length) return std::nullopt;
    return next;
}

} // namespace detail

namespace {

constexpr std::uint64_t kMaxDecodedFrames = static_cast<std::uint64_t>(format::kMaxDurationSeconds) * format::kSampleRate;
constexpr std::size_t kMaxDecodedFloatValues = 128U * 1024U * 1024U;

#ifdef RENDEROJN_EXTERNAL_DEPS
struct MemoryFile {
    const std::vector<std::uint8_t>& bytes;
    sf_count_t position{};
};

sf_count_t memory_length(void* user) {
    return static_cast<sf_count_t>(static_cast<MemoryFile*>(user)->bytes.size());
}

sf_count_t memory_seek(sf_count_t offset, int whence, void* user) {
    auto& file = *static_cast<MemoryFile*>(user);
    sf_count_t base{};
    if (whence == SEEK_SET) base = 0;
    else if (whence == SEEK_CUR) base = file.position;
    else if (whence == SEEK_END) base = memory_length(user);
    else return -1;
    const auto next = detail::checked_memory_seek_position(static_cast<std::int64_t>(base), static_cast<std::int64_t>(offset),
                                                            static_cast<std::int64_t>(memory_length(user)));
    if (!next) return -1;
    file.position = static_cast<sf_count_t>(*next);
    return file.position;
}

sf_count_t memory_read(void* destination, sf_count_t count, void* user) {
    auto& file = *static_cast<MemoryFile*>(user);
    if (count < 0) return 0;
    const auto available = memory_length(user) - file.position;
    const auto read = std::min(count, available);
    if (read > 0) {
        std::memcpy(destination, file.bytes.data() + file.position, static_cast<std::size_t>(read));
        file.position += read;
    }
    return read;
}

sf_count_t memory_write(const void*, sf_count_t, void*) { return 0; }
sf_count_t memory_tell(void* user) { return static_cast<MemoryFile*>(user)->position; }

format::DecodedSample decode_one(const format::EncodedSample& sample, Diagnostics& diagnostics) {
    std::optional<NormalizedOggPayload> normalized_ogg;
    if (sample.bytes.size() >= 4U && std::memcmp(sample.bytes.data(), "OggS", 4) == 0) {
        normalized_ogg.emplace(normalize_ogg_vorbis_pages(sample, diagnostics));
    }
    const auto& encoded = normalized_ogg ? normalized_ogg->bytes() : sample.bytes;
    MemoryFile memory{encoded};
    SF_VIRTUAL_IO io{memory_length, memory_seek, memory_read, memory_write, memory_tell};
    SF_INFO info{};
    SNDFILE* handle = sf_open_virtual(&io, SFM_READ, &info, &memory);
    if (handle == nullptr) {
        throw Error(ExitCode::Runtime, "Unsupported or corrupt encoded sample " + std::to_string(sample.id) + ": " + sf_strerror(nullptr));
    }
    const auto close = [&handle]() { if (handle != nullptr) sf_close(handle); };
    try {
        if (info.frames < 0 || info.samplerate <= 0 || info.channels <= 0 || info.channels > 64 ||
            static_cast<std::uint64_t>(info.frames) > kMaxDecodedFrames) {
            throw Error(ExitCode::Runtime, "Decoded sample " + std::to_string(sample.id) + " has impossible dimensions");
        }
        const auto source_frames = static_cast<std::size_t>(info.frames);
        if (source_frames == 0) throw Error(ExitCode::Runtime, "Decoded sample " + std::to_string(sample.id) + " has no audio frames");
        std::size_t source_values{};
        if (!io::checked_multiply(source_frames, static_cast<std::size_t>(info.channels), source_values) || source_values > kMaxDecodedFloatValues) {
            throw Error(ExitCode::Runtime, "Decoded sample " + std::to_string(sample.id) + " is too large");
        }
        std::vector<float> source(source_values);
        const auto read = sf_readf_float(handle, source.data(), info.frames);
        if (read != info.frames) throw Error(ExitCode::Runtime, "Unable to decode complete sample " + std::to_string(sample.id));
        const auto output_frames_u64 = (static_cast<std::uint64_t>(source_frames) * format::kSampleRate +
                                        static_cast<std::uint64_t>(info.samplerate) - 1U) / static_cast<std::uint64_t>(info.samplerate);
        if (output_frames_u64 > kMaxDecodedFrames) throw Error(ExitCode::Runtime, "Resampled sample " + std::to_string(sample.id) + " is too long");
        const auto output_frames = static_cast<std::size_t>(output_frames_u64);
        std::size_t output_values{};
        if (!io::checked_multiply(output_frames, 2U, output_values) || output_values > kMaxDecodedFloatValues) {
            throw Error(ExitCode::Runtime, "Resampled sample " + std::to_string(sample.id) + " is too large");
        }
        std::vector<float> output(output_values);
        for (std::size_t frame = 0; frame < output_frames; ++frame) {
            const double source_position = static_cast<double>(frame) * static_cast<double>(info.samplerate) / format::kSampleRate;
            const auto left_index = std::min(static_cast<std::size_t>(source_position), source_frames - 1U);
            const auto right_index = std::min(left_index + 1U, source_frames - 1U);
            const float fraction = static_cast<float>(source_position - static_cast<double>(left_index));
            const auto interpolate = [&](int channel) {
                const int selected = info.channels == 1 ? 0 : std::min(channel, info.channels - 1);
                const float before = source[left_index * static_cast<std::size_t>(info.channels) + static_cast<std::size_t>(selected)];
                const float after = source[right_index * static_cast<std::size_t>(info.channels) + static_cast<std::size_t>(selected)];
                return before + (after - before) * fraction;
            };
            output[frame * 2U] = interpolate(0);
            output[frame * 2U + 1U] = interpolate(1);
        }
        close();
        return {sample.id, std::move(output)};
    } catch (...) {
        close();
        throw;
    }
}
#endif

} // namespace

std::vector<format::DecodedSample> decode_samples(const format::Package& package, Diagnostics& diagnostics) {
#ifdef RENDEROJN_EXTERNAL_DEPS
    std::vector<format::DecodedSample> decoded;
    decoded.reserve(package.samples.size());
    for (const auto& sample : package.samples) decoded.push_back(decode_one(sample, diagnostics));
    return decoded;
#else
    static_cast<void>(package);
    static_cast<void>(diagnostics);
    throw Error(ExitCode::Runtime, "This RenderOJN build has no libsndfile support; configure with declared vcpkg dependencies.");
#endif
}

} // namespace renderojn::audio
