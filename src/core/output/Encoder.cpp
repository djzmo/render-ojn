#include "core/output/Encoder.hpp"

#include "core/Diagnostic.hpp"
#include "core/io/File.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

#ifdef RENDEROJN_EXTERNAL_DEPS
#include <lame/lame.h>
#include <sndfile.h>
#include <fileref.h>
#include <tag.h>
#include <xiphcomment.h>
#endif

namespace renderojn::output {
namespace {

constexpr std::uint32_t kSampleRate = 48000;

void write_u16(std::ostream& stream, std::uint16_t value) {
    const std::array<char, 2> bytes{{static_cast<char>(value & 0xffU), static_cast<char>((value >> 8U) & 0xffU)}};
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void write_u32(std::ostream& stream, std::uint32_t value) {
    const std::array<char, 4> bytes{{static_cast<char>(value & 0xffU), static_cast<char>((value >> 8U) & 0xffU),
                                     static_cast<char>((value >> 16U) & 0xffU), static_cast<char>((value >> 24U) & 0xffU)}};
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void write_wav(io::TransactionalFile& output, std::uint64_t frames, const PcmProducer& produce) {
    if (frames > (std::numeric_limits<std::uint32_t>::max() - 36U) / 4U) {
        throw Error(ExitCode::Runtime, "WAV output exceeds the 4 GiB RIFF limit");
    }
    auto& stream = output.stream();
    stream.write("RIFF", 4); write_u32(stream, static_cast<std::uint32_t>(frames * 4U + 36U)); stream.write("WAVE", 4);
    stream.write("fmt ", 4); write_u32(stream, 16); write_u16(stream, 1); write_u16(stream, 2); write_u32(stream, kSampleRate);
    write_u32(stream, kSampleRate * 4U); write_u16(stream, 4); write_u16(stream, 16); stream.write("data", 4);
    write_u32(stream, static_cast<std::uint32_t>(frames * 4U));
    std::uint64_t written{};
    produce([&](const float* values, std::size_t block_frames) {
        std::vector<std::int16_t> pcm(block_frames * 2U);
        for (std::size_t index = 0; index < pcm.size(); ++index) {
            const auto clipped = std::clamp(values[index], -1.0F, 1.0F);
            const auto scaled = std::lround(clipped * 32767.0F);
            pcm[index] = static_cast<std::int16_t>(std::clamp<long>(scaled, -32768L, 32767L));
        }
        stream.write(reinterpret_cast<const char*>(pcm.data()), static_cast<std::streamsize>(pcm.size() * sizeof(std::int16_t)));
        written += block_frames;
    });
    if (written != frames || !stream) throw Error(ExitCode::Runtime, "Renderer did not produce the expected WAV frame count");
}

#ifdef RENDEROJN_EXTERNAL_DEPS
void tag_file(const std::filesystem::path& path, const Tags& tags, bool ogg) {
    TagLib::FileRef file(path.string().c_str());
    if (file.isNull() || file.tag() == nullptr) throw Error(ExitCode::Runtime, "Unable to tag output: " + path.string());
    auto* tag = file.tag();
    tag->setTitle(TagLib::String(tags.title.c_str(), TagLib::String::Latin1));
    tag->setArtist(TagLib::String(tags.artist.c_str(), TagLib::String::Latin1));
    tag->setTrack(tags.track);
    tag->setGenre(TagLib::String(tags.genre.c_str(), TagLib::String::Latin1));
    tag->setComment(TagLib::String(tags.comment.c_str(), TagLib::String::UTF8));
    if (ogg) {
        if (auto* xiph = dynamic_cast<TagLib::Ogg::XiphComment*>(tag)) xiph->removeFields("ENCODER");
    }
    if (!file.save()) throw Error(ExitCode::Runtime, "Unable to save output tags: " + path.string());
}

// Single-owner LAME handle.  A manual close in both the success path and a
// catch-all handler can release the same encoder twice when closing or tagging
// throws, so ownership lives here and release is idempotent.
class LameHandle {
public:
    LameHandle() : lame_(lame_init()) {
        if (lame_ == nullptr) throw Error(ExitCode::Runtime, "Unable to initialize LAME");
    }
    ~LameHandle() { reset(); }
    LameHandle(const LameHandle&) = delete;
    LameHandle& operator=(const LameHandle&) = delete;

    [[nodiscard]] lame_t get() const noexcept { return lame_; }
    void reset() noexcept {
        if (lame_ != nullptr) {
            lame_close(lame_);
            lame_ = nullptr;
        }
    }

private:
    lame_t lame_{};
};

void write_mp3(io::TransactionalFile& output, std::uint64_t frames, int quality, const Tags& tags, const PcmProducer& produce) {
    LameHandle lame;
    lame_set_num_channels(lame.get(), 2); lame_set_in_samplerate(lame.get(), kSampleRate); lame_set_mode(lame.get(), JOINT_STEREO);
    const auto mapping = mp3_quality_for(quality);
    lame_set_brate(lame.get(), mapping.bitrate_kbps);
    lame_set_quality(lame.get(), mapping.lame_quality);
    if (lame_init_params(lame.get()) < 0) throw Error(ExitCode::Runtime, "Unable to configure LAME");
    std::uint64_t written{};
    produce([&](const float* values, std::size_t block_frames) {
        std::vector<unsigned char> encoded(static_cast<std::size_t>(1.25 * static_cast<double>(block_frames)) + 7200U);
        const auto count = lame_encode_buffer_interleaved_ieee_float(lame.get(), const_cast<float*>(values), static_cast<int>(block_frames), encoded.data(), static_cast<int>(encoded.size()));
        if (count < 0) throw Error(ExitCode::Runtime, "LAME failed while encoding audio");
        output.stream().write(reinterpret_cast<const char*>(encoded.data()), count);
        written += block_frames;
    });
    std::array<unsigned char, 7200> tail{};
    const auto count = lame_encode_flush(lame.get(), tail.data(), static_cast<int>(tail.size()));
    if (count < 0) throw Error(ExitCode::Runtime, "LAME failed while finalizing audio");
    output.stream().write(reinterpret_cast<const char*>(tail.data()), count);
    if (written != frames || !output.stream()) throw Error(ExitCode::Runtime, "Renderer did not produce the expected MP3 frame count");
    // Release the encoder before the file is closed and tagged so a tagging
    // failure cannot leave the handle open or close it a second time.
    lame.reset();
    output.close();
    tag_file(output.temporary_path(), tags, false);
}

// Single-owner libsndfile handle, for the same reason as LameHandle.
class SndFileHandle {
public:
    explicit SndFileHandle(SNDFILE* file) noexcept : file_(file) {}
    ~SndFileHandle() { reset(); }
    SndFileHandle(const SndFileHandle&) = delete;
    SndFileHandle& operator=(const SndFileHandle&) = delete;

    [[nodiscard]] SNDFILE* get() const noexcept { return file_; }
    void reset() noexcept {
        if (file_ != nullptr) {
            sf_close(file_);
            file_ = nullptr;
        }
    }

private:
    SNDFILE* file_{};
};

void write_ogg(io::TransactionalFile& output, std::uint64_t frames, int quality, const Tags& tags, const PcmProducer& produce) {
    output.close();
    SF_INFO info{};
    info.samplerate = kSampleRate;
    info.channels = 2;
    info.format = SF_FORMAT_OGG | SF_FORMAT_VORBIS;
    SndFileHandle file(sf_open(output.temporary_path().string().c_str(), SFM_WRITE, &info));
    if (file.get() == nullptr) throw Error(ExitCode::Runtime, "Unable to create Ogg output: " + std::string(sf_strerror(nullptr)));
    double vbr = ogg_quality_for(quality);
    sf_command(file.get(), SFC_SET_VBR_ENCODING_QUALITY, &vbr, sizeof(vbr));
    std::uint64_t written{};
    produce([&](const float* values, std::size_t block_frames) {
        if (sf_writef_float(file.get(), values, static_cast<sf_count_t>(block_frames)) != static_cast<sf_count_t>(block_frames)) {
            throw Error(ExitCode::Runtime, "Unable to encode Ogg audio");
        }
        written += block_frames;
    });
    // The stream must be finalized before tagging reopens the same path.
    file.reset();
    if (written != frames) throw Error(ExitCode::Runtime, "Renderer did not produce the expected Ogg frame count");
    tag_file(output.temporary_path(), tags, true);
}
#endif

} // namespace

// Quality 3 is the default and the highest setting; anything below 2 collapses
// onto the lowest tier so an out-of-range value can never widen the output.
Mp3Quality mp3_quality_for(int quality) noexcept {
    if (quality >= 3) return {320, 2};
    if (quality == 2) return {192, 5};
    return {128, 7};
}

double ogg_quality_for(int quality) noexcept {
    if (quality >= 3) return 1.0;
    if (quality == 2) return 0.8;
    return 0.5;
}

void encode_transactionally(Format format, const std::filesystem::path& destination, std::uint64_t frames, int quality,
                            const Tags& tags, const PcmProducer& produce) {
    io::TransactionalFile output(destination);
    if (format == Format::Wav) {
        write_wav(output, frames, produce);
    } else {
#ifdef RENDEROJN_EXTERNAL_DEPS
        if (format == Format::Mp3) write_mp3(output, frames, quality, tags, produce);
        else write_ogg(output, frames, quality, tags, produce);
#else
        static_cast<void>(frames); static_cast<void>(quality); static_cast<void>(tags); static_cast<void>(produce);
        throw Error(ExitCode::Runtime, "This RenderOJN build has no MP3/Ogg dependencies; configure with declared vcpkg dependencies.");
#endif
    }
    output.commit();
}

} // namespace renderojn::output
