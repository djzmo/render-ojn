#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace renderojn::output {

enum class Format { Wav, Mp3, Ogg };

struct Tags {
    std::string title;
    std::string artist;
    std::uint32_t track{};
    std::string genre;
    std::string comment;
    // Front-cover picture, embedded verbatim when non-empty (ID3v2 APIC for
    // MP3, METADATA_BLOCK_PICTURE for Ogg).  cover_mime names its format.
    std::vector<std::uint8_t> cover;
    std::string cover_mime;
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

// Encodes to memory instead of the filesystem, for hosts that have no writable
// path to publish to -- the WebAssembly build in particular.  Both entry points
// run the same encoders over the same sink abstraction, so a buffer and a file
// produced from identical input are byte-for-byte identical.  Tagging applies to
// MP3 and Ogg exactly as it does on disk; WAV carries no tags in either path.
[[nodiscard]] std::vector<std::uint8_t> encode_to_buffer(Format format, std::uint64_t frames, int quality,
                                                         const Tags& tags, const PcmProducer& produce);

} // namespace renderojn::output
