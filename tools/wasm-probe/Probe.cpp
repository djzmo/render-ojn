// Step 0 toolchain probe.
//
// Proves, under wasm32-emscripten, that:
//   1. libsndfile can WRITE WAV through an in-memory SF_VIRTUAL_IO sink
//      (the seam the WASM encoder needs, mirroring Decoder.cpp's read path).
//   2. libsndfile can WRITE Ogg/Vorbis the same way  <-- the actual gate.
//   3. mp3lame can encode MP3 to a memory buffer.
//   4. TagLib can tag MP3 and Ogg buffers via ByteVectorStream, no filesystem.
//
// Exits non-zero and prints the failing stage on any failure.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <lame/lame.h>
#include <sndfile.h>

#include <fileref.h>
#include <tag.h>
#include <tbytevectorstream.h>
#include <xiphcomment.h>

namespace {

constexpr int kSampleRate = 48000;
constexpr int kChannels = 2;
constexpr int kFrames = kSampleRate; // one second

// A 440 Hz stereo sine, so a decoder sees real signal rather than silence.
std::vector<float> make_tone() {
    std::vector<float> pcm(static_cast<std::size_t>(kFrames) * kChannels);
    for (int frame = 0; frame < kFrames; ++frame) {
        const auto value = static_cast<float>(
            0.25 * std::sin(2.0 * 3.14159265358979323846 * 440.0 * frame / kSampleRate));
        pcm[static_cast<std::size_t>(frame) * 2] = value;
        pcm[static_cast<std::size_t>(frame) * 2 + 1] = value;
    }
    return pcm;
}

// Writable in-memory sink for libsndfile. Decoder.cpp already does this in the
// read direction; this is the same idea with write/seek implemented.
struct MemorySink {
    std::vector<std::uint8_t> bytes;
    sf_count_t position{};
};

sf_count_t sink_length(void* user) {
    return static_cast<sf_count_t>(static_cast<MemorySink*>(user)->bytes.size());
}

sf_count_t sink_seek(sf_count_t offset, int whence, void* user) {
    auto& sink = *static_cast<MemorySink*>(user);
    const auto size = static_cast<sf_count_t>(sink.bytes.size());
    sf_count_t target = 0;
    if (whence == SEEK_SET) target = offset;
    else if (whence == SEEK_CUR) target = sink.position + offset;
    else if (whence == SEEK_END) target = size + offset;
    else return -1;
    if (target < 0) return -1;
    if (target > size) sink.bytes.resize(static_cast<std::size_t>(target), 0);
    sink.position = target;
    return sink.position;
}

sf_count_t sink_read(void* destination, sf_count_t count, void* user) {
    auto& sink = *static_cast<MemorySink*>(user);
    if (count <= 0) return 0;
    const auto size = static_cast<sf_count_t>(sink.bytes.size());
    // A seek may have parked the cursor at or past EOF; clamp rather than
    // computing a negative "available" and memcpy'ing a huge unsigned length.
    if (sink.position >= size) return 0;
    const auto available = size - sink.position;
    const auto read = count < available ? count : available;
    std::memcpy(destination, sink.bytes.data() + sink.position, static_cast<std::size_t>(read));
    sink.position += read;
    return read;
}

sf_count_t sink_write(const void* source, sf_count_t count, void* user) {
    auto& sink = *static_cast<MemorySink*>(user);
    if (count <= 0) return 0;
    const auto end = sink.position + count;
    // Grow first, then take .data() -- resize may reallocate, so a pointer
    // captured beforehand would dangle.  Vorbis also rewinds to patch its
    // headers, so `position` can sit anywhere inside the buffer.
    if (end > static_cast<sf_count_t>(sink.bytes.size())) {
        sink.bytes.resize(static_cast<std::size_t>(end), 0);
    }
    std::memcpy(sink.bytes.data() + sink.position, source, static_cast<std::size_t>(count));
    sink.position = end;
    return count;
}

sf_count_t sink_tell(void* user) { return static_cast<MemorySink*>(user)->position; }

std::vector<std::uint8_t> encode_with_sndfile(int format, const std::vector<float>& pcm,
                                              const char* label, bool vbr) {
    MemorySink sink;
    SF_VIRTUAL_IO io{sink_length, sink_seek, sink_read, sink_write, sink_tell};
    SF_INFO info{};
    info.samplerate = kSampleRate;
    info.channels = kChannels;
    info.format = format;

    SNDFILE* handle = sf_open_virtual(&io, SFM_WRITE, &info, &sink);
    if (handle == nullptr) {
        std::printf("FAIL  %s: sf_open_virtual: %s\n", label, sf_strerror(nullptr));
        return {};
    }
    if (vbr) {
        double quality = 0.6;
        sf_command(handle, SFC_SET_VBR_ENCODING_QUALITY, &quality, sizeof(quality));
    }
    const auto written = sf_writef_float(handle, pcm.data(), kFrames);
    sf_close(handle);
    if (written != kFrames) {
        std::printf("FAIL  %s: wrote %lld of %d frames\n", label,
                    static_cast<long long>(written), kFrames);
        return {};
    }
    return sink.bytes;
}

std::vector<std::uint8_t> encode_mp3(const std::vector<float>& pcm) {
    lame_t lame = lame_init();
    if (lame == nullptr) {
        std::printf("FAIL  mp3: lame_init\n");
        return {};
    }
    lame_set_num_channels(lame, kChannels);
    lame_set_in_samplerate(lame, kSampleRate);
    lame_set_mode(lame, JOINT_STEREO);
    lame_set_brate(lame, 192);
    lame_set_quality(lame, 2);
    if (lame_init_params(lame) < 0) {
        std::printf("FAIL  mp3: lame_init_params\n");
        lame_close(lame);
        return {};
    }
    std::vector<std::uint8_t> out(static_cast<std::size_t>(1.25 * kFrames) + 7200);
    const auto count = lame_encode_buffer_interleaved_ieee_float(
        lame, const_cast<float*>(pcm.data()), kFrames, out.data(), static_cast<int>(out.size()));
    if (count < 0) {
        std::printf("FAIL  mp3: encode returned %d\n", count);
        lame_close(lame);
        return {};
    }
    std::vector<std::uint8_t> tail(7200);
    const auto flushed = lame_encode_flush(lame, tail.data(), static_cast<int>(tail.size()));
    lame_close(lame);
    if (flushed < 0) {
        std::printf("FAIL  mp3: flush returned %d\n", flushed);
        return {};
    }
    out.resize(static_cast<std::size_t>(count));
    out.insert(out.end(), tail.begin(), tail.begin() + flushed);
    return out;
}

// The §3 tagging approach: FileRef over ByteVectorStream, no path involved.
bool tag_buffer(std::vector<std::uint8_t>& bytes, bool ogg, const char* label) {
    TagLib::ByteVectorStream stream(
        TagLib::ByteVector(reinterpret_cast<const char*>(bytes.data()),
                           static_cast<unsigned int>(bytes.size())));
    TagLib::FileRef file(&stream);
    if (file.isNull() || file.tag() == nullptr) {
        std::printf("FAIL  tag/%s: FileRef null over ByteVectorStream\n", label);
        return false;
    }
    auto* tag = file.tag();
    tag->setTitle(TagLib::String("Probe Title", TagLib::String::Latin1));
    tag->setArtist(TagLib::String("Probe Artist", TagLib::String::Latin1));
    tag->setTrack(1);
    tag->setGenre(TagLib::String("Probe Genre", TagLib::String::Latin1));
    tag->setComment(TagLib::String("probe", TagLib::String::UTF8));
    if (ogg) {
        if (auto* xiph = dynamic_cast<TagLib::Ogg::XiphComment*>(tag)) xiph->removeFields("ENCODER");
    }
    if (!file.save()) {
        std::printf("FAIL  tag/%s: save\n", label);
        return false;
    }
    const auto* data = stream.data();
    bytes.assign(data->begin(), data->end());

    // Read the tag back from the rewritten buffer: proves the bytes are real.
    TagLib::ByteVectorStream verify(TagLib::ByteVector(
        reinterpret_cast<const char*>(bytes.data()), static_cast<unsigned int>(bytes.size())));
    TagLib::FileRef reread(&verify);
    if (reread.isNull() || reread.tag() == nullptr) {
        std::printf("FAIL  tag/%s: reread\n", label);
        return false;
    }
    if (reread.tag()->title() != TagLib::String("Probe Title", TagLib::String::Latin1)) {
        std::printf("FAIL  tag/%s: title did not round-trip\n", label);
        return false;
    }
    return true;
}

// Decode back through libsndfile to prove the bytes are a real stream.
bool verify_decodes(const std::vector<std::uint8_t>& bytes, const char* label, int expect_format) {
    MemorySink source;
    source.bytes = bytes;
    SF_VIRTUAL_IO io{sink_length, sink_seek, sink_read, sink_write, sink_tell};
    SF_INFO info{};
    SNDFILE* handle = sf_open_virtual(&io, SFM_READ, &info, &source);
    if (handle == nullptr) {
        std::printf("FAIL  %s: not decodable: %s\n", label, sf_strerror(nullptr));
        return false;
    }
    const bool ok = info.channels == kChannels && info.samplerate == kSampleRate;
    const int subtype = info.format & SF_FORMAT_SUBMASK;
    sf_close(handle);
    if (!ok) {
        std::printf("FAIL  %s: decoded %d ch @ %d Hz\n", label, info.channels, info.samplerate);
        return false;
    }
    if (expect_format != 0 && subtype != expect_format) {
        std::printf("FAIL  %s: unexpected subtype 0x%x\n", label, subtype);
        return false;
    }
    return true;
}

} // namespace

int main() {
    // Report what libsndfile was actually built with. If Vorbis is absent the
    // format list is where it shows up first.
    char version[128]{};
    sf_command(nullptr, SFC_GET_LIB_VERSION, version, sizeof(version));
    std::printf("libsndfile: %s\n", version);

    int major_count = 0;
    sf_command(nullptr, SFC_GET_FORMAT_MAJOR_COUNT, &major_count, sizeof(major_count));
    bool ogg_container = false;
    for (int index = 0; index < major_count; ++index) {
        SF_FORMAT_INFO info{};
        info.format = index;
        sf_command(nullptr, SFC_GET_FORMAT_MAJOR, &info, sizeof(info));
        if ((info.format & SF_FORMAT_TYPEMASK) == SF_FORMAT_OGG) ogg_container = true;
    }
    std::printf("ogg container advertised: %s\n", ogg_container ? "yes" : "no");

    const auto pcm = make_tone();
    bool ok = true;

    // 1. WAV
    auto wav = encode_with_sndfile(SF_FORMAT_WAV | SF_FORMAT_PCM_16, pcm, "wav", false);
    if (wav.empty() || !verify_decodes(wav, "wav", SF_FORMAT_PCM_16)) ok = false;
    else std::printf("PASS  wav   %zu bytes\n", wav.size());

    // 2. OGG/VORBIS -- the gate.
    auto ogg = encode_with_sndfile(SF_FORMAT_OGG | SF_FORMAT_VORBIS, pcm, "ogg", true);
    if (ogg.empty() || !verify_decodes(ogg, "ogg", SF_FORMAT_VORBIS)) ok = false;
    else std::printf("PASS  ogg   %zu bytes\n", ogg.size());

    // 3. MP3 via LAME.
    auto mp3 = encode_mp3(pcm);
    if (mp3.empty()) ok = false;
    else std::printf("PASS  mp3   %zu bytes\n", mp3.size());

    // 4. Tagging, in memory, both tagged formats.
    if (!mp3.empty()) {
        if (!tag_buffer(mp3, false, "mp3")) ok = false;
        else std::printf("PASS  tag/mp3 -> %zu bytes\n", mp3.size());
    }
    if (!ogg.empty()) {
        if (!tag_buffer(ogg, true, "ogg")) ok = false;
        else std::printf("PASS  tag/ogg -> %zu bytes\n", ogg.size());
    }

    std::printf("%s\n", ok ? "STEP0 OK" : "STEP0 FAILED");
    return ok ? 0 : 1;
}
