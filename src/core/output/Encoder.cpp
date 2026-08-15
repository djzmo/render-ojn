#include "core/output/Encoder.hpp"

#include "core/Diagnostic.hpp"
#include "core/io/File.hpp"
#include "core/io/Path.hpp"

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
#include <tbytevectorstream.h>
#include <xiphcomment.h>
#endif

namespace renderojn::output {
namespace {

constexpr std::uint32_t kSampleRate = 48000;

// Where encoded bytes land.  A file-backed sink writes straight through to the
// transactional stream; a memory-backed sink accumulates into a vector for hosts
// with no writable filesystem.  Every encoder below targets this rather than a
// stream or a path, so the on-disk and in-memory paths cannot diverge.
class ByteSink {
public:
    virtual ~ByteSink() = default;
    virtual void write(const void* data, std::size_t size) = 0;
    // True while the underlying destination is still healthy.  A file sink can
    // fail mid-write; a memory sink cannot.
    [[nodiscard]] virtual bool good() const = 0;
};

class StreamSink final : public ByteSink {
public:
    explicit StreamSink(std::ostream& stream) noexcept : stream_(stream) {}
    void write(const void* data, std::size_t size) override {
        stream_.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    }
    [[nodiscard]] bool good() const override { return static_cast<bool>(stream_); }

private:
    std::ostream& stream_;
};

class BufferSink final : public ByteSink {
public:
    void write(const void* data, std::size_t size) override {
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        bytes_.insert(bytes_.end(), bytes, bytes + size);
    }
    [[nodiscard]] bool good() const override { return true; }
    [[nodiscard]] std::vector<std::uint8_t>& bytes() noexcept { return bytes_; }

private:
    std::vector<std::uint8_t> bytes_;
};

void write_u16(ByteSink& sink, std::uint16_t value) {
    const std::array<char, 2> bytes{{static_cast<char>(value & 0xffU), static_cast<char>((value >> 8U) & 0xffU)}};
    sink.write(bytes.data(), bytes.size());
}

void write_u32(ByteSink& sink, std::uint32_t value) {
    const std::array<char, 4> bytes{{static_cast<char>(value & 0xffU), static_cast<char>((value >> 8U) & 0xffU),
                                     static_cast<char>((value >> 16U) & 0xffU), static_cast<char>((value >> 24U) & 0xffU)}};
    sink.write(bytes.data(), bytes.size());
}

void write_wav(ByteSink& sink, std::uint64_t frames, const PcmProducer& produce) {
    if (frames > (std::numeric_limits<std::uint32_t>::max() - 36U) / 4U) {
        throw Error(ExitCode::Runtime, "WAV output exceeds the 4 GiB RIFF limit");
    }
    sink.write("RIFF", 4); write_u32(sink, static_cast<std::uint32_t>(frames * 4U + 36U)); sink.write("WAVE", 4);
    sink.write("fmt ", 4); write_u32(sink, 16); write_u16(sink, 1); write_u16(sink, 2); write_u32(sink, kSampleRate);
    write_u32(sink, kSampleRate * 4U); write_u16(sink, 4); write_u16(sink, 16); sink.write("data", 4);
    write_u32(sink, static_cast<std::uint32_t>(frames * 4U));
    std::uint64_t written{};
    produce([&](const float* values, std::size_t block_frames) {
        std::vector<std::int16_t> pcm(block_frames * 2U);
        for (std::size_t index = 0; index < pcm.size(); ++index) {
            const auto clipped = std::clamp(values[index], -1.0F, 1.0F);
            const auto scaled = std::lround(clipped * 32767.0F);
            pcm[index] = static_cast<std::int16_t>(std::clamp<long>(scaled, -32768L, 32767L));
        }
        sink.write(pcm.data(), pcm.size() * sizeof(std::int16_t));
        written += block_frames;
    });
    if (written != frames || !sink.good()) throw Error(ExitCode::Runtime, "Renderer did not produce the expected WAV frame count");
}

#ifdef RENDEROJN_EXTERNAL_DEPS
// The single definition of what a RenderOJN tag set looks like.  Both the file
// and buffer paths call this, so neither can drift from the other.
void apply_tags(TagLib::Tag& tag, const Tags& tags, bool ogg) {
    tag.setTitle(TagLib::String(tags.title, TagLib::String::UTF8));
    tag.setArtist(TagLib::String(tags.artist, TagLib::String::UTF8));
    tag.setTrack(tags.track);
    tag.setGenre(TagLib::String(tags.genre, TagLib::String::UTF8));
    tag.setComment(TagLib::String(tags.comment, TagLib::String::UTF8));
    if (ogg) {
        if (auto* xiph = dynamic_cast<TagLib::Ogg::XiphComment*>(&tag)) xiph->removeFields("ENCODER");
    }
}

void tag_file(const std::filesystem::path& path, const Tags& tags, bool ogg) {
    // path::c_str() is wchar_t* on Windows and char* elsewhere; TagLib::FileName
    // accepts both, so this opens Unicode paths correctly on every platform.
    TagLib::FileRef file(path.c_str());
    if (file.isNull() || file.tag() == nullptr) throw Error(ExitCode::Runtime, "Unable to tag output: " + io::path_to_utf8(path));
    apply_tags(*file.tag(), tags, ogg);
    if (!file.save()) throw Error(ExitCode::Runtime, "Unable to save output tags: " + io::path_to_utf8(path));
}

// Tags an already-encoded buffer in place.  TagLib's ByteVectorStream is a full
// IOStream implementation, so this needs no filesystem at all -- which is what
// makes tagging work in the WebAssembly build.
void tag_buffer(std::vector<std::uint8_t>& bytes, const Tags& tags, bool ogg) {
    TagLib::ByteVectorStream stream(TagLib::ByteVector(reinterpret_cast<const char*>(bytes.data()),
                                                      static_cast<unsigned int>(bytes.size())));
    TagLib::FileRef file(&stream);
    if (file.isNull() || file.tag() == nullptr) throw Error(ExitCode::Runtime, "Unable to tag encoded audio");
    apply_tags(*file.tag(), tags, ogg);
    if (!file.save()) throw Error(ExitCode::Runtime, "Unable to save tags onto encoded audio");
    const auto* data = stream.data();
    bytes.assign(data->begin(), data->end());
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

// Encodes the MP3 body only.  Tagging is the caller's job because the two hosts
// finalize differently: a file must be closed before TagLib reopens the path,
// while a buffer is simply rewritten in place.
void write_mp3(ByteSink& sink, std::uint64_t frames, int quality, const PcmProducer& produce) {
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
        sink.write(encoded.data(), static_cast<std::size_t>(count));
        written += block_frames;
    });
    std::array<unsigned char, 7200> tail{};
    const auto count = lame_encode_flush(lame.get(), tail.data(), static_cast<int>(tail.size()));
    if (count < 0) throw Error(ExitCode::Runtime, "LAME failed while finalizing audio");
    sink.write(tail.data(), static_cast<std::size_t>(count));
    if (written != frames || !sink.good()) throw Error(ExitCode::Runtime, "Renderer did not produce the expected MP3 frame count");
    // Release the encoder before the caller closes or tags the output so a
    // tagging failure cannot leave the handle open or close it a second time.
    lame.reset();
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

// Streams the Vorbis body through an already-open libsndfile handle.  Both hosts
// share this so the encoder settings cannot drift; only the handle differs.
void stream_ogg(SndFileHandle& file, std::uint64_t frames, int quality, const PcmProducer& produce) {
    double vbr = ogg_quality_for(quality);
    sf_command(file.get(), SFC_SET_VBR_ENCODING_QUALITY, &vbr, sizeof(vbr));
    std::uint64_t written{};
    produce([&](const float* values, std::size_t block_frames) {
        if (sf_writef_float(file.get(), values, static_cast<sf_count_t>(block_frames)) != static_cast<sf_count_t>(block_frames)) {
            throw Error(ExitCode::Runtime, "Unable to encode Ogg audio");
        }
        written += block_frames;
    });
    // The stream must be finalized before anything reads the encoded bytes back.
    file.reset();
    if (written != frames) throw Error(ExitCode::Runtime, "Renderer did not produce the expected Ogg frame count");
}

void write_ogg(io::TransactionalFile& output, std::uint64_t frames, int quality, const Tags& tags, const PcmProducer& produce) {
    output.close();
    SF_INFO info{};
    info.samplerate = kSampleRate;
    info.channels = 2;
    info.format = SF_FORMAT_OGG | SF_FORMAT_VORBIS;
    // On Windows libsndfile's sf_open decodes a char* path through the active
    // code page (sndfile.c, CP_ACP), which corrupts any name outside the user's
    // locale; sf_wchar_open takes the wide path directly.
#ifdef _WIN32
    SndFileHandle file(sf_wchar_open(output.temporary_path().c_str(), SFM_WRITE, &info));
#else
    SndFileHandle file(sf_open(output.temporary_path().c_str(), SFM_WRITE, &info));
#endif
    if (file.get() == nullptr) throw Error(ExitCode::Runtime, "Unable to create Ogg output: " + std::string(sf_strerror(nullptr)));
    stream_ogg(file, frames, quality, produce);
    tag_file(output.temporary_path(), tags, true);
}

// Writable in-memory backing for libsndfile.  Decoder.cpp already reads samples
// through SF_VIRTUAL_IO; this is the same seam in the write direction, and it is
// what lets Ogg encoding work with no filesystem.
struct VirtualBuffer {
    std::vector<std::uint8_t> bytes;
    sf_count_t position{};
};

sf_count_t virtual_length(void* user) {
    return static_cast<sf_count_t>(static_cast<VirtualBuffer*>(user)->bytes.size());
}

sf_count_t virtual_seek(sf_count_t offset, int whence, void* user) {
    auto& buffer = *static_cast<VirtualBuffer*>(user);
    const auto size = static_cast<sf_count_t>(buffer.bytes.size());
    sf_count_t target{};
    if (whence == SEEK_SET) target = offset;
    else if (whence == SEEK_CUR) target = buffer.position + offset;
    else if (whence == SEEK_END) target = size + offset;
    else return -1;
    if (target < 0) return -1;
    // Vorbis rewinds to patch its headers, and may seek past the current end
    // before writing there; grow so the following write lands in bounds.
    if (target > size) buffer.bytes.resize(static_cast<std::size_t>(target), 0);
    buffer.position = target;
    return buffer.position;
}

sf_count_t virtual_read(void* destination, sf_count_t count, void* user) {
    auto& buffer = *static_cast<VirtualBuffer*>(user);
    if (count <= 0) return 0;
    const auto size = static_cast<sf_count_t>(buffer.bytes.size());
    if (buffer.position >= size) return 0;
    const auto available = size - buffer.position;
    const auto read = count < available ? count : available;
    std::memcpy(destination, buffer.bytes.data() + buffer.position, static_cast<std::size_t>(read));
    buffer.position += read;
    return read;
}

sf_count_t virtual_write(const void* source, sf_count_t count, void* user) {
    auto& buffer = *static_cast<VirtualBuffer*>(user);
    if (count <= 0) return 0;
    const auto end = buffer.position + count;
    // Grow before taking .data(): resize may reallocate.
    if (end > static_cast<sf_count_t>(buffer.bytes.size())) {
        buffer.bytes.resize(static_cast<std::size_t>(end), 0);
    }
    std::memcpy(buffer.bytes.data() + buffer.position, source, static_cast<std::size_t>(count));
    buffer.position = end;
    return count;
}

sf_count_t virtual_tell(void* user) { return static_cast<VirtualBuffer*>(user)->position; }

std::vector<std::uint8_t> write_ogg_buffer(std::uint64_t frames, int quality, const Tags& tags, const PcmProducer& produce) {
    VirtualBuffer buffer;
    SF_VIRTUAL_IO io{virtual_length, virtual_seek, virtual_read, virtual_write, virtual_tell};
    SF_INFO info{};
    info.samplerate = kSampleRate;
    info.channels = 2;
    info.format = SF_FORMAT_OGG | SF_FORMAT_VORBIS;
    SndFileHandle file(sf_open_virtual(&io, SFM_WRITE, &info, &buffer));
    if (file.get() == nullptr) throw Error(ExitCode::Runtime, "Unable to create Ogg output: " + std::string(sf_strerror(nullptr)));
    stream_ogg(file, frames, quality, produce);
    tag_buffer(buffer.bytes, tags, true);
    return std::move(buffer.bytes);
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
        StreamSink sink(output.stream());
        write_wav(sink, frames, produce);
    } else {
#ifdef RENDEROJN_EXTERNAL_DEPS
        if (format == Format::Mp3) {
            StreamSink sink(output.stream());
            write_mp3(sink, frames, quality, produce);
            // Close before tagging: TagLib reopens the same path.
            output.close();
            tag_file(output.temporary_path(), tags, false);
        } else {
            write_ogg(output, frames, quality, tags, produce);
        }
#else
        static_cast<void>(frames); static_cast<void>(quality); static_cast<void>(tags); static_cast<void>(produce);
        throw Error(ExitCode::Runtime, "This RenderOJN build has no MP3/Ogg dependencies; configure with declared vcpkg dependencies.");
#endif
    }
    output.commit();
}

std::vector<std::uint8_t> encode_to_buffer(Format format, std::uint64_t frames, int quality, const Tags& tags,
                                           const PcmProducer& produce) {
    if (format == Format::Wav) {
        BufferSink sink;
        write_wav(sink, frames, produce);
        return std::move(sink.bytes());
    }
#ifdef RENDEROJN_EXTERNAL_DEPS
    if (format == Format::Mp3) {
        BufferSink sink;
        write_mp3(sink, frames, quality, produce);
        auto bytes = std::move(sink.bytes());
        tag_buffer(bytes, tags, false);
        return bytes;
    }
    return write_ogg_buffer(frames, quality, tags, produce);
#else
    static_cast<void>(frames); static_cast<void>(quality); static_cast<void>(tags); static_cast<void>(produce);
    throw Error(ExitCode::Runtime, "This RenderOJN build has no MP3/Ogg dependencies; configure with declared vcpkg dependencies.");
#endif
}

} // namespace renderojn::output
