#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace renderojn::output {

enum class Format { Wav, Mp3, Ogg };

struct Tags {
    std::string title;
    std::string artist;
    std::uint32_t track{};
    std::string genre;
    std::string comment;
};

using PcmProducer = std::function<void(const std::function<void(const float*, std::size_t)>&)>;

// The quality selector is a small public contract, so the mappings are exposed
// for direct testing rather than being buried in the encoder bodies.
struct Mp3Quality {
    int bitrate_kbps{};
    int lame_quality{};
};

[[nodiscard]] Mp3Quality mp3_quality_for(int quality) noexcept;
[[nodiscard]] double ogg_quality_for(int quality) noexcept;

void encode_transactionally(Format format, const std::filesystem::path& destination, std::uint64_t frames, int quality,
                            const Tags& tags, const PcmProducer& produce);

} // namespace renderojn::output
