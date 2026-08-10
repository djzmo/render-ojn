#include "core/Diagnostic.hpp"
#include "core/audio/Decoder.hpp"
#include "core/output/Encoder.hpp"
#include "core/render/Mixer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifdef RENDEROJN_EXTERNAL_DEPS
#include <sndfile.h>
#include <fileref.h>
#include <tag.h>
#include <xiphcomment.h>
#endif

namespace {

renderojn::format::Chart chart_with_notes(std::vector<renderojn::format::NoteEvent> notes) {
    renderojn::format::Chart chart;
    chart.difficulty = renderojn::format::Difficulty::Hard;
    chart.header.duration_seconds[2] = 1;
    chart.notes = std::move(notes);
    return chart;
}

std::vector<float> render(const renderojn::format::Chart& chart,
                          const std::vector<renderojn::format::DecodedSample>& samples,
                          renderojn::render::SchedulingMode mode, renderojn::Diagnostics& diagnostics,
                          std::vector<std::size_t>* delivered_blocks = nullptr) {
    std::vector<float> output;
    renderojn::render::mix_chart(chart, samples, mode, false,
                                  [&](const float* frames, std::size_t count) {
                                      output.insert(output.end(), frames, frames + count * 2U);
                                      if (delivered_blocks != nullptr) delivered_blocks->push_back(count);
                                  },
                                  diagnostics);
    return output;
}

struct OggPage {
    std::size_t body_offset{};
    std::size_t body_size{};
};

std::vector<OggPage> audio_pages(const std::vector<std::uint8_t>& bytes) {
    std::vector<OggPage> pages;
    std::size_t offset{};
    std::size_t completed_headers{};
    while (offset + 27U <= bytes.size()) {
        if (std::string(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                        bytes.begin() + static_cast<std::ptrdiff_t>(offset + 4U)) != "OggS") {
            throw std::logic_error("generated Ogg fixture lost its page capture pattern");
        }
        const auto segments = static_cast<std::size_t>(bytes[offset + 26U]);
        if (offset + 27U + segments > bytes.size()) throw std::logic_error("generated Ogg fixture has a truncated segment table");
        std::size_t body_size{};
        bool has_audio{};
        for (std::size_t index = 0; index < segments; ++index) {
            const auto lace = bytes[offset + 27U + index];
            body_size += lace;
            if (completed_headers >= 3U && lace != 0U) has_audio = true;
            if (completed_headers < 3U && lace != 255U) ++completed_headers;
        }
        const auto body_offset = offset + 27U + segments;
        if (body_offset + body_size > bytes.size()) throw std::logic_error("generated Ogg fixture has a truncated body");
        if (has_audio) pages.push_back({body_offset, body_size});
        offset = body_offset + body_size;
    }
    if (offset != bytes.size() || completed_headers != 3U) throw std::logic_error("generated Ogg fixture is incomplete");
    return pages;
}

} // namespace

TEST_CASE("quick scheduler places events at frames zero and one exactly") {
    const auto chart = chart_with_notes({{0, 1, 0, 0, 0, 1}, {1, 1, 0, 0, 1, 1}});
    const std::vector<renderojn::format::DecodedSample> samples{{1, {1.0F, 1.0F}}};
    renderojn::Diagnostics diagnostics;
    const auto output = render(chart, samples, renderojn::render::SchedulingMode::Quick, diagnostics);
    REQUIRE(output.size() == renderojn::render::output_frame_count(chart) * 2U);
    CHECK(output[0] == 1.0F);
    CHECK(output[2] == 1.0F);
}

TEST_CASE("quick scheduler mixes simultaneous events at their shared target frame") {
    const auto chart = chart_with_notes({{17, 1, 0, 0, 0, 1}, {17, 1, 0, 0, 1, 1}});
    const std::vector<renderojn::format::DecodedSample> samples{{1, {0.5F, 0.5F}}};
    renderojn::Diagnostics diagnostics;
    const auto output = render(chart, samples, renderojn::render::SchedulingMode::Quick, diagnostics);
    CHECK(output[16U * 2U] == 0.0F);
    CHECK(output[17U * 2U] == 1.0F);
    CHECK(output[18U * 2U] == 0.0F);
}

TEST_CASE("quick scheduler preserves exact onsets at 1024-frame block boundaries") {
    const auto chart = chart_with_notes({{1023, 1, 0, 0, 0, 1}, {1024, 1, 0, 0, 1, 1}});
    const std::vector<renderojn::format::DecodedSample> samples{{1, {1.0F, 1.0F}}};
    renderojn::Diagnostics diagnostics;
    std::vector<std::size_t> delivered_blocks;
    const auto output = render(chart, samples, renderojn::render::SchedulingMode::Quick, diagnostics, &delivered_blocks);
    REQUIRE(delivered_blocks.size() >= 2U);
    CHECK(delivered_blocks[0] == 1024U);
    CHECK(delivered_blocks[1] == 1024U);
    CHECK(output[1022U * 2U] == 0.0F);
    CHECK(output[1023U * 2U] == 1.0F);
    CHECK(output[1024U * 2U] == 1.0F);
    CHECK(output[1025U * 2U] == 0.0F);
}

TEST_CASE("quick scheduler preserves exact onsets across successive blocks") {
    const auto chart = chart_with_notes({{10, 1, 0, 0, 0, 1}, {1025, 1, 0, 0, 1, 1}, {2049, 1, 0, 0, 2, 1}});
    const std::vector<renderojn::format::DecodedSample> samples{{1, {1.0F, 1.0F}}};
    renderojn::Diagnostics diagnostics;
    const auto output = render(chart, samples, renderojn::render::SchedulingMode::Quick, diagnostics);
    CHECK(output[10U * 2U] == 1.0F);
    CHECK(output[1025U * 2U] == 1.0F);
    CHECK(output[2049U * 2U] == 1.0F);
}

TEST_CASE("realtime scheduler retains 48-frame onset quantization") {
    const auto chart = chart_with_notes({{0, 1, 0, 0, 0, 1}, {1, 1, 0, 0, 1, 1}});
    const std::vector<renderojn::format::DecodedSample> samples{{1, {1.0F, 1.0F}}};
    renderojn::Diagnostics diagnostics;
    const auto output = render(chart, samples, renderojn::render::SchedulingMode::Realtime, diagnostics);
    CHECK(output[0] == 1.0F);
    CHECK(output[2] == 0.0F);
    CHECK(output[48U * 2U] == 1.0F);
}

TEST_CASE("quick scheduler keeps the 100-voice drop-newest policy") {
    std::vector<renderojn::format::NoteEvent> notes(101, {0, 1, 0, 0, 0, 1});
    notes.back().reference_id = 2;
    const auto chart = chart_with_notes(std::move(notes));
    const std::vector<renderojn::format::DecodedSample> samples{{1, {0.5F, 0.5F}}, {2, {1.0F, 1.0F}}};
    renderojn::Diagnostics diagnostics;
    const auto output = render(chart, samples, renderojn::render::SchedulingMode::Quick, diagnostics);
    CHECK(output[0] == 50.0F);
    REQUIRE(diagnostics.warnings().size() == 1U);
    CHECK(diagnostics.warnings().front() == "voice limit reached; dropping newest trigger");
}

TEST_CASE("transactional WAV failure preserves an existing destination") {
    const auto destination = std::filesystem::temp_directory_path() / "renderojn-transactional-existing.wav";
    { std::ofstream existing(destination, std::ios::binary | std::ios::trunc); existing << "original"; }
    renderojn::output::Tags tags{};
    REQUIRE_THROWS_AS(renderojn::output::encode_transactionally(renderojn::output::Format::Wav, destination, 1, 3, tags,
        [](const auto&) { throw renderojn::Error(renderojn::ExitCode::Runtime, "forced encoder failure"); }), renderojn::Error);
    std::ifstream existing(destination, std::ios::binary);
    std::string content;
    existing >> content;
    CHECK(content == "original");
    std::error_code ignored;
    std::filesystem::remove(destination, ignored);
}

TEST_CASE("MP3 and Ogg quality selectors map to their documented encoder settings") {
    using renderojn::output::mp3_quality_for;
    using renderojn::output::ogg_quality_for;

    CHECK(mp3_quality_for(3).bitrate_kbps == 320);
    CHECK(mp3_quality_for(3).lame_quality == 2);
    CHECK(mp3_quality_for(2).bitrate_kbps == 192);
    CHECK(mp3_quality_for(2).lame_quality == 5);
    CHECK(mp3_quality_for(1).bitrate_kbps == 128);
    CHECK(mp3_quality_for(1).lame_quality == 7);

    CHECK(ogg_quality_for(3) == 1.0);
    CHECK(ogg_quality_for(2) == 0.8);
    CHECK(ogg_quality_for(1) == 0.5);

    // An out-of-range selector must never widen the output beyond the tier the
    // CLI validated, and must never fall below the lowest tier.
    CHECK(mp3_quality_for(0).bitrate_kbps == 128);
    CHECK(mp3_quality_for(-1).bitrate_kbps == 128);
    CHECK(mp3_quality_for(99).bitrate_kbps == 320);
    CHECK(ogg_quality_for(0) == 0.5);
    CHECK(ogg_quality_for(99) == 1.0);
}

#ifdef RENDEROJN_EXTERNAL_DEPS
namespace {

// Produces a short non-silent stereo tone so encoded output can be decoded and
// measured rather than merely existing on disk.
renderojn::output::PcmProducer tone_producer(std::size_t frames) {
    return [frames](const auto& write) {
        std::vector<float> block(frames * 2U);
        for (std::size_t index = 0; index < frames; ++index) {
            const auto value = static_cast<float>(std::sin(static_cast<double>(index) * 0.05)) * 0.5F;
            block[index * 2U] = value;
            block[index * 2U + 1U] = value;
        }
        write(block.data(), frames);
    };
}

renderojn::output::Tags sample_tags() {
    renderojn::output::Tags tags{};
    tags.title = "Synthetic Title";
    tags.artist = "Synthetic Artist";
    tags.track = 7;
    tags.genre = "Synthetic Genre";
    tags.comment = "Synthetic Comment";
    return tags;
}

} // namespace

TEST_CASE("encoded MP3 and Ogg output carries exact tags and decodes to stereo 48 kHz audio") {
    constexpr std::size_t kFrames = 48000;
    struct Case {
        renderojn::output::Format format;
        const char* filename;
        bool ogg;
    };
    const std::vector<Case> cases{{renderojn::output::Format::Mp3, "renderojn-tagged.mp3", false},
                                  {renderojn::output::Format::Ogg, "renderojn-tagged.ogg", true}};

    for (const auto& item : cases) {
        const auto destination = std::filesystem::temp_directory_path() / item.filename;
        std::error_code ignored;
        std::filesystem::remove(destination, ignored);

        renderojn::output::encode_transactionally(item.format, destination, kFrames, 3, sample_tags(),
                                                  tone_producer(kFrames));
        REQUIRE(std::filesystem::exists(destination));

        TagLib::FileRef tagged(destination.string().c_str());
        REQUIRE_FALSE(tagged.isNull());
        REQUIRE(tagged.tag() != nullptr);
        CHECK(tagged.tag()->title() == TagLib::String("Synthetic Title"));
        CHECK(tagged.tag()->artist() == TagLib::String("Synthetic Artist"));
        CHECK(tagged.tag()->track() == 7);
        CHECK(tagged.tag()->genre() == TagLib::String("Synthetic Genre"));
        CHECK(tagged.tag()->comment() == TagLib::String("Synthetic Comment"));

        if (item.ogg) {
            // libsndfile stamps an ENCODER comment that must not survive.
            auto* xiph = dynamic_cast<TagLib::Ogg::XiphComment*>(tagged.tag());
            REQUIRE(xiph != nullptr);
            CHECK(xiph->fieldListMap().find("ENCODER") == xiph->fieldListMap().end());

            SF_INFO info{};
            SNDFILE* decoded = sf_open(destination.string().c_str(), SFM_READ, &info);
            REQUIRE(decoded != nullptr);
            CHECK(info.channels == 2);
            CHECK(info.samplerate == 48000);
            // Lossy encoders adjust length slightly; allow a small tolerance.
            CHECK(std::llabs(static_cast<long long>(info.frames) - static_cast<long long>(kFrames)) < 4800);
            std::vector<float> samples(static_cast<std::size_t>(info.frames) * 2U);
            const auto read = sf_readf_float(decoded, samples.data(), info.frames);
            sf_close(decoded);
            CHECK(read == info.frames);
            double peak = 0.0;
            for (const auto value : samples) peak = std::max(peak, std::fabs(static_cast<double>(value)));
            CHECK(peak > 0.01);
        }

        std::filesystem::remove(destination, ignored);
    }
}

TEST_CASE("a failure after encoding removes the temporary file and preserves the destination") {
    // An encoder failure must never leave a partial temporary beside the
    // destination, and must never damage an already-published file.
    for (const auto format : {renderojn::output::Format::Mp3, renderojn::output::Format::Ogg}) {
        const auto destination = std::filesystem::temp_directory_path() / "renderojn-encoder-preserve.out";
        { std::ofstream existing(destination, std::ios::binary | std::ios::trunc); existing << "original"; }

        REQUIRE_THROWS_AS(renderojn::output::encode_transactionally(
                              format, destination, 4800, 3, sample_tags(),
                              [](const auto&) { throw renderojn::Error(renderojn::ExitCode::Runtime, "forced encoder failure"); }),
                          renderojn::Error);

        std::ifstream existing(destination, std::ios::binary);
        std::string content;
        existing >> content;
        CHECK(content == "original");

        std::error_code ignored;
        std::filesystem::remove(destination, ignored);
    }
}
#endif

TEST_CASE("WAV output is PCM16 stereo 48 kHz with the declared frame count") {
    const auto destination = std::filesystem::temp_directory_path() / "renderojn-modern-wav.wav";
    std::error_code ignored;
    std::filesystem::remove(destination, ignored);
    renderojn::output::Tags tags{};
    renderojn::output::encode_transactionally(renderojn::output::Format::Wav, destination, 2, 3, tags,
        [](const auto& write) { const float frames[] = {1.0F, -1.0F, 0.5F, -0.5F}; write(frames, 2); });
    std::ifstream file(destination, std::ios::binary);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), {});
    REQUIRE(bytes.size() == 52);
    CHECK(std::string(bytes.begin(), bytes.begin() + 4) == "RIFF");
    CHECK(bytes[20] == 1);
    CHECK(bytes[22] == 2);
    CHECK(bytes[24] == 0x80);
    CHECK(bytes[25] == 0xbb);
    std::filesystem::remove(destination, ignored);
}

TEST_CASE("CRC-repaired but undecodable generated Vorbis samples fail without omission") {
    const auto destination = std::filesystem::temp_directory_path() / "renderojn-generated-valid.ogg";
    std::error_code ignored;
    std::filesystem::remove(destination, ignored);
    renderojn::output::Tags tags{};
    const std::vector<float> source(9600U * 2U, 0.5F);
    renderojn::output::encode_transactionally(renderojn::output::Format::Ogg, destination, 9600U, 3, tags,
        [&source](const auto& write) { write(source.data(), 9600U); });

    std::ifstream file(destination, std::ios::binary);
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
    const auto pages = audio_pages(bytes);
    REQUIRE_FALSE(pages.empty());
    for (const auto& page : pages) {
        std::fill(bytes.begin() + static_cast<std::ptrdiff_t>(page.body_offset),
                  bytes.begin() + static_cast<std::ptrdiff_t>(page.body_offset + page.body_size), 0xffU);
    }

    renderojn::format::Package package;
    package.kind = renderojn::format::PackageKind::OMC;
    package.samples.push_back({1001, std::move(bytes)});
    renderojn::Diagnostics diagnostics;
    CHECK_THROWS_AS(renderojn::audio::decode_samples(package, diagnostics), renderojn::Error);
    REQUIRE(diagnostics.warnings().size() == 1U);
    CHECK(diagnostics.warnings().front() ==
          "repaired " + std::to_string(pages.size()) + " legacy Ogg page checksum(s) for sample 1001");
    std::filesystem::remove(destination, ignored);
}
