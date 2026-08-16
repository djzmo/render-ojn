// WebAssembly entry points for the browser renderer.
//
// This file is deliberately thin: it owns no rendering logic of its own, and
// mirrors the CLI's sequence in src/app/Main.cpp step for step.  Anything that
// decides how audio sounds -- compatibility profiles, scheduling mode, tag
// contents -- must stay in the shared core so the web build and the desktop
// build cannot drift apart.

#include "core/Diagnostic.hpp"
#include "core/audio/Decoder.hpp"
#include "core/compat/CompatibilityProfile.hpp"
#include "core/format/OjnParser.hpp"
#include "core/format/PackageParser.hpp"
#include "core/format/Types.hpp"
#include "core/io/ByteReader.hpp"
#include "core/output/Encoder.hpp"
#include "core/render/Mixer.hpp"

#include "app/ChartTags.hpp"

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace renderojn;

// Rejects bad arguments as a real JavaScript Error rather than a C++ one.
// A thrown renderojn::Error crosses into JS as an opaque CppException whose
// message is not reachable from the catch site -- it stringifies as
// "[object Object]" -- so argument validation would be unreadable in the UI.
[[noreturn]] void throw_js_error(const std::string& message) {
    emscripten::val::global("Error").new_(message).throw_();
}

// Takes the bytes from a JS Uint8Array.  Deliberately not std::string: embind
// marshals std::string as UTF-8 and silently mangles any byte above 0x7F, which
// destroys binary input -- an OJN's very first field is a little-endian song id.
std::shared_ptr<const io::ByteBuffer> buffer_from(const emscripten::val& bytes, const char* what) {
    // Reject anything that is not a Uint8Array before touching it.  Without
    // this, `null` escapes as a raw JS TypeError the UI cannot present, and an
    // ArrayBuffer -- which File.arrayBuffer() returns, and which has no
    // `length` -- yields an empty buffer that fails later as a misleading
    // "truncated header" rather than as the type error it actually is.
    const auto global_u8 = emscripten::val::global("Uint8Array");
    if (bytes.isUndefined() || bytes.isNull() || !bytes.instanceof(global_u8)) {
        throw_js_error(std::string("Expected a Uint8Array of ") + what + " bytes");
    }
    const auto length = bytes["length"].as<unsigned>();
    std::vector<std::uint8_t> copy(length);
    // Wrap the destination in a typed array view over WASM memory and let JS do
    // the copy; this is the supported way to move a TypedArray across.
    emscripten::val view(emscripten::typed_memory_view(copy.size(), copy.data()));
    view.call<void>("set", bytes);
    return std::make_shared<const io::ByteBuffer>(std::move(copy));
}

// What the preview card needs.  Deliberately reachable from the OJN alone: the
// UI shows title and artist the moment a chart is dropped, without waiting for
// the user to supply its sample package.
struct DifficultyInfo {
    int difficulty{};
    std::uint32_t noteCount{};
    std::uint32_t durationSeconds{};
};

struct OjnInfo {
    std::string title;
    std::string artist;
    std::string charter;
    std::string packageName;
    std::string genre;
    std::uint32_t songId{};
    float tempo{};
    std::vector<DifficultyInfo> difficulties;
    std::vector<std::string> warnings;
};

struct RenderResult {
    // emscripten::val so the encoded bytes cross into JS as a typed array
    // without a second copy through std::string.
    emscripten::val bytes = emscripten::val::undefined();
    std::vector<std::string> warnings;
};

OjnInfo read_ojn_info(const emscripten::val& ojn_bytes) {
    Diagnostics diagnostics;
    const auto ojn = format::normalize_ojn(buffer_from(ojn_bytes, "OJN"));
    const auto header = format::parse_ojn_header(ojn);

    OjnInfo info;
    info.title = header.title;
    info.artist = header.artist;
    info.charter = header.charter;
    info.packageName = header.package_name;
    info.genre = format::genre_name(header.genre, diagnostics);
    info.songId = header.song_id;
    info.tempo = header.tempo;
    for (std::size_t index = 0; index < header.note_counts.size(); ++index) {
        // A difficulty with no events is not present in the file; offering it in
        // the UI would produce an empty render.
        if (header.event_counts[index] == 0) continue;
        DifficultyInfo entry;
        entry.difficulty = static_cast<int>(index);
        entry.noteCount = header.note_counts[index];
        entry.durationSeconds = header.duration_seconds[index];
        info.difficulties.push_back(entry);
    }
    info.warnings = diagnostics.warnings();
    return info;
}

// Renders a chart to encoded audio.  `on_progress` is invoked with a 0..1
// fraction as mixing proceeds; it may be undefined.
// Named render_chart rather than render: `using namespace renderojn` makes the
// renderojn::render namespace visible, and a bare `render` is ambiguous.
RenderResult render_chart(const emscripten::val& ojn_bytes, const emscripten::val& ojm_bytes, int difficulty,
                          int format_value, int quality, int tracks, int render_mode, emscripten::val on_progress) {
    Diagnostics diagnostics;

    if (difficulty < 0 || difficulty > 2) {
        throw_js_error("Difficulty must be 0 (Easy), 1 (Normal), or 2 (Hard)");
    }
    if (format_value < 0 || format_value > 2) {
        throw_js_error("Output format must be 0 (WAV), 1 (MP3), or 2 (OGG)");
    }
    // Mirrors render::TrackSelection {All = 0, Keysounds = 1, Background = 2}.
    if (tracks < 0 || tracks > 2) {
        throw_js_error("Tracks must be 0 (All), 1 (Keysounds), or 2 (Background)");
    }
    // Mirrors render::SchedulingMode {Quick = 0, Realtime = 1}.
    if (render_mode < 0 || render_mode > 1) {
        throw_js_error("Render mode must be 0 (Quick) or 1 (Realtime)");
    }
    // Same range the CLI enforces (src/app/Cli.cpp).  Without this an
    // out-of-range value falls through to the lowest bitrate tier in
    // mp3_quality_for/ogg_quality_for and silently degrades the output.
    if (quality < 1 || quality > 3) {
        throw_js_error("Quality must be 1, 2, or 3");
    }

    const auto ojn = buffer_from(ojn_bytes, "OJN");
    const auto package_data = buffer_from(ojm_bytes, "sample package");

    const auto chart = format::parse_ojn_chart(ojn, static_cast<format::Difficulty>(difficulty));
    const auto& header = chart.header;

    // The normalized buffer is what the compat profiles hash, exactly as the CLI
    // does -- a `new`-wrapped OJN must hash as its decrypted form.
    const auto normalized = format::normalize_ojn(ojn);
    auto compatible_chart = chart;
    compat::apply_known_ojn_ojm_profiles(compatible_chart, *normalized, *package_data, diagnostics);

    const auto package = format::parse_sample_package(package_data);
    if (package.empty_pcm_slot_records != 0) {
        diagnostics.warn("sample package contains " + std::to_string(package.empty_pcm_slot_records) +
                         " empty PCM directory slot record(s); ignored");
    }
    if (package.empty_ogg_sentinel_records != 0) {
        diagnostics.warn("sample package contains " + std::to_string(package.empty_ogg_sentinel_records) +
                         " empty Ogg directory slot record(s); ignored");
    }
    const auto samples = audio::decode_samples(package, diagnostics);

    // Shared with the CLI so the tag set (and the cover) cannot drift.  The web
    // build always embeds the cover, matching the CLI default.
    const auto tags = app::build_chart_tags(header, *normalized, /*include_cover=*/true, diagnostics);

    const auto total_frames = render::output_frame_count(compatible_chart);
    const bool report_progress = !on_progress.isUndefined() && !on_progress.isNull();

    std::uint64_t produced{};
    // Realtime scheduling delivers 48-frame blocks, so a full-length chart would
    // fire tens of thousands of progress callbacks -- each one a worker message
    // and a React render.  Report only when the fraction has advanced by at
    // least this much (and always the final 1.0 below), which caps it near 200
    // updates regardless of block size.
    constexpr double kProgressStep = 0.005;
    double last_reported = -1.0;
    const auto encoded = output::encode_to_buffer(
        static_cast<output::Format>(format_value), total_frames, quality, tags,
        [&](const auto& consume) {
            // Realtime *scheduling* (48-frame blocks with onset quantization) is
            // offered as an option, but wall-clock pacing is not: it would sleep
            // for the length of the song, which makes no sense for a file the
            // user is downloading.  So the mode is honored, the pacing flag is
            // always false.
            render::mix_chart(compatible_chart, samples, static_cast<render::SchedulingMode>(render_mode), false,
                              [&](const float* frames, std::size_t frame_count) {
                                  consume(frames, frame_count);
                                  produced += frame_count;
                                  if (report_progress && total_frames > 0) {
                                      const double fraction =
                                          static_cast<double>(produced) / static_cast<double>(total_frames);
                                      if (fraction - last_reported >= kProgressStep) {
                                          last_reported = fraction;
                                          on_progress(fraction);
                                      }
                                  }
                              },
                              diagnostics, static_cast<render::TrackSelection>(tracks));
        });
    // A final 1.0 so the bar always lands full even if the last step was small.
    if (report_progress && total_frames > 0 && last_reported < 1.0) on_progress(1.0);

    RenderResult result;
    result.bytes = emscripten::val(emscripten::typed_memory_view(encoded.size(), encoded.data()));
    // typed_memory_view aliases WASM memory, which the next allocation may
    // invalidate; hand JavaScript its own copy before returning.
    result.bytes = result.bytes.call<emscripten::val>("slice");
    result.warnings = diagnostics.warnings();
    return result;
}

// A renderojn::Error thrown through embind reaches JavaScript as an opaque
// handle carrying only a pointer -- it stringifies as "[object Object]", and
// getExceptionMessage faults on it rather than reading the message back.  So
// the message has to cross the boundary as a real JS Error, exactly as
// argument validation already does.  Without this every core failure (bad
// signature, truncated header, undecodable sample) collapses into one
// indistinguishable string on the page.
template <typename Fn>
auto with_js_errors(Fn&& fn) -> decltype(fn()) {
    try {
        return fn();
    } catch (const Error& error) {
        throw_js_error(error.what());
    } catch (const std::exception& error) {
        throw_js_error(error.what());
    }
}

OjnInfo read_ojn_info_js(const emscripten::val& ojn_bytes) {
    return with_js_errors([&] { return read_ojn_info(ojn_bytes); });
}

RenderResult render_chart_js(const emscripten::val& ojn_bytes, const emscripten::val& ojm_bytes, int difficulty,
                             int format_value, int quality, int tracks, int render_mode, emscripten::val on_progress) {
    return with_js_errors([&] {
        return render_chart(ojn_bytes, ojm_bytes, difficulty, format_value, quality, tracks, render_mode, on_progress);
    });
}

} // namespace

EMSCRIPTEN_BINDINGS(renderojn) {
    emscripten::value_object<DifficultyInfo>("DifficultyInfo")
        .field("difficulty", &DifficultyInfo::difficulty)
        .field("noteCount", &DifficultyInfo::noteCount)
        .field("durationSeconds", &DifficultyInfo::durationSeconds);

    emscripten::value_object<OjnInfo>("OjnInfo")
        .field("title", &OjnInfo::title)
        .field("artist", &OjnInfo::artist)
        .field("charter", &OjnInfo::charter)
        .field("packageName", &OjnInfo::packageName)
        .field("genre", &OjnInfo::genre)
        .field("songId", &OjnInfo::songId)
        .field("tempo", &OjnInfo::tempo)
        .field("difficulties", &OjnInfo::difficulties)
        .field("warnings", &OjnInfo::warnings);

    emscripten::value_object<RenderResult>("RenderResult")
        .field("bytes", &RenderResult::bytes)
        .field("warnings", &RenderResult::warnings);

    emscripten::register_vector<DifficultyInfo>("DifficultyInfoVector");
    emscripten::register_vector<std::string>("StringVector");

    // The _js wrappers translate core errors into JavaScript Errors so their
    // messages survive the boundary; nothing should bind the raw functions.
    emscripten::function("readOjnInfo", &read_ojn_info_js);
    emscripten::function("render", &render_chart_js);
}
