#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace renderojn::format {

constexpr std::uint32_t kSampleRate = 48000;
constexpr std::uint32_t kMaxEvents = 1000000;
constexpr std::uint32_t kMaxSamples = 100000;
constexpr std::uint32_t kMaxDurationSeconds = 6 * 60 * 60;

enum class Difficulty : std::uint8_t { Easy = 0, Normal = 1, Hard = 2 };
enum class PackageKind : std::uint8_t { M30, OMC, OJM };

struct OjnHeader {
    std::uint32_t song_id{};
    std::uint32_t genre{};
    float tempo{};
    std::array<std::uint32_t, 3> event_counts{};
    std::array<std::uint32_t, 3> note_counts{};
    std::array<std::uint32_t, 3> measure_counts{};
    std::array<std::uint32_t, 3> package_counts{};
    std::string title;
    std::string artist;
    std::string charter;
    std::string package_name;
    std::array<std::uint32_t, 3> duration_seconds{};
    std::array<std::uint32_t, 4> chart_offsets{};
    // Cover art trails the last chart section at chart_offsets[3]: a JPEG of
    // new_cover_size bytes, then a BMP of old_cover_size bytes.  Either may be 0.
    std::uint32_t old_cover_size{};
    std::uint32_t new_cover_size{};
};

struct NoteEvent {
    std::uint64_t frame{};
    std::uint16_t reference_id{};
    std::uint8_t note_type{};
    std::uint32_t measure{};
    std::uint16_t slot_index{};
    std::uint16_t slot_count{};
    // The chart carries each note on a channel: 2-8 are the playable lanes the
    // player hits (keysounds), 9+ are autoplay/background events the game
    // triggers on its own. The parser knows this only while reading the event
    // stream; recording it here is what lets the mixer render one role in
    // isolation (see render::TrackSelection).
    bool is_keysound{};
};

struct Chart {
    OjnHeader header;
    Difficulty difficulty{Difficulty::Hard};
    std::vector<NoteEvent> notes;
};

struct EncodedSample {
    std::uint16_t id{};
    std::vector<std::uint8_t> bytes;
};

struct Package {
    PackageKind kind{};
    std::vector<EncodedSample> samples;
    std::uint32_t empty_pcm_slot_records{};
    // Some game-authored OJM files contain fully formed, zero-byte Ogg directory
    // entries after the header's declared sample count. They do not name audio
    // payloads, but are retained here so the CLI can report their handling.
    std::uint32_t empty_ogg_sentinel_records{};
};

struct DecodedSample {
    std::uint16_t id{};
    std::vector<float> stereo_frames;
};

} // namespace renderojn::format
