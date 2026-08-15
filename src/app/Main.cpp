#include "app/Cli.hpp"
#include "app/Version.hpp"
#include "core/Diagnostic.hpp"
#include "core/audio/Decoder.hpp"
#include "core/audio/Playback.hpp"
#include "core/compat/CompatibilityProfile.hpp"
#include "core/format/OjnParser.hpp"
#include "core/format/PackageParser.hpp"
#include "core/io/ByteReader.hpp"
#include "core/io/Path.hpp"
#include "core/output/Encoder.hpp"
#include "core/render/Mixer.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {

constexpr std::uint64_t kTwoGiB = 2ULL * 1024ULL * 1024ULL * 1024ULL;

std::filesystem::path resolve_sample_package(const renderojn::app::Options& options, const std::filesystem::path& input,
                                             const renderojn::format::OjnHeader& header, renderojn::Diagnostics& diagnostics) {
    namespace fs = std::filesystem;
    if (options.sample_package) {
        if (!fs::is_regular_file(*options.sample_package)) {
            throw renderojn::Error(renderojn::ExitCode::Runtime, "Sample-package override does not exist: " + renderojn::io::path_to_utf8(*options.sample_package));
        }
        return *options.sample_package;
    }
    if (header.package_name.empty()) {
        throw renderojn::Error(renderojn::ExitCode::Runtime, "OJN does not name a sample package; pass --sample-package <path>");
    }
    const auto beside_input = input.parent_path() / renderojn::io::utf8_to_path(header.package_name);
    if (fs::is_regular_file(beside_input)) return beside_input;
    const auto current_directory = fs::current_path() / renderojn::io::utf8_to_path(header.package_name);
    if (fs::is_regular_file(current_directory)) {
        diagnostics.warn("sample package was resolved from the current directory; place it beside the OJN or use --sample-package");
        return current_directory;
    }
    throw renderojn::Error(renderojn::ExitCode::Runtime, "Unable to locate sample package '" + header.package_name + "' beside the OJN or in the current directory");
}

renderojn::output::Format map_format(renderojn::app::OutputFormat value) {
    using AppFormat = renderojn::app::OutputFormat;
    if (value == AppFormat::Wav) return renderojn::output::Format::Wav;
    if (value == AppFormat::Mp3) return renderojn::output::Format::Mp3;
    return renderojn::output::Format::Ogg;
}

renderojn::render::SchedulingMode map_mode(renderojn::app::RenderMode value) {
    return value == renderojn::app::RenderMode::Quick ? renderojn::render::SchedulingMode::Quick : renderojn::render::SchedulingMode::Realtime;
}

void print_warnings(const renderojn::Diagnostics& diagnostics, const char* indent = "") {
    for (const auto& warning : diagnostics.warnings()) std::cerr << indent << "warning: " << warning << '\n';
}

// Renders (or plays) one chart end to end and returns the published path --
// empty under --play.  Everything that decides how a chart sounds lives in the
// shared core; this only sequences it, exactly as src/wasm/Bindings.cpp does.
std::filesystem::path render_one(const renderojn::app::Options& options, const std::filesystem::path& input,
                                 renderojn::Diagnostics& diagnostics) {
    const auto ojn = renderojn::io::read_file(input, kTwoGiB, "OJN");
    // A Korea-era `new` wrapper is decrypted here once; the compat profiles hash
    // the decrypted form, as the WebAssembly build does.
    const auto normalized = renderojn::format::normalize_ojn(ojn);
    // parse_ojn_chart parses and validates the header itself, so reuse the copy
    // it returns instead of running the whole header validation twice.
    const auto chart = renderojn::format::parse_ojn_chart(normalized, options.difficulty);
    const auto& header = chart.header;
    // The output name may come from the header, so it resolves only now.
    const auto destination = options.play ? std::filesystem::path{} : renderojn::app::resolve_output_path(options, input, header.title);

    const auto package_path = resolve_sample_package(options, input, header, diagnostics);
    const auto package_data = renderojn::io::read_file(package_path, kTwoGiB, "sample package");
    auto compatible_chart = chart;
    renderojn::compat::apply_known_ojn_ojm_profiles(compatible_chart, *normalized, *package_data, diagnostics);
    const auto package = renderojn::format::parse_sample_package(package_data);
    if (package.empty_pcm_slot_records != 0) {
        diagnostics.warn("sample package contains " + std::to_string(package.empty_pcm_slot_records) +
                         " empty PCM directory slot record(s); ignored");
    }
    if (package.empty_ogg_sentinel_records != 0) {
        diagnostics.warn("sample package contains " + std::to_string(package.empty_ogg_sentinel_records) +
                         " empty Ogg directory slot record(s); ignored");
    }
    const auto samples = renderojn::audio::decode_samples(package, diagnostics);

    if (options.play) {
        renderojn::audio::play_realtime(compatible_chart, samples, diagnostics);
        return {};
    }
    renderojn::output::Tags tags;
    tags.title = header.title;
    tags.artist = header.artist;
    tags.track = header.song_id;
    tags.genre = renderojn::format::genre_name(header.genre, diagnostics);
    tags.comment = std::string("Generated by RenderOJN v") + renderojn::app::version::kVersion + "\n" + renderojn::app::version::kProjectUrl;
    if (options.cover_art) {
        if (auto cover = renderojn::format::extract_cover_art(*normalized, header, diagnostics)) {
            tags.cover = std::move(cover->bytes);
            tags.cover_mime = std::move(cover->mime);
        }
    }
    const auto mode = map_mode(options.render_mode);
    renderojn::output::encode_transactionally(map_format(options.output_format), destination, renderojn::render::output_frame_count(compatible_chart),
                                               options.quality, tags,
                                               [&](const auto& consumer) {
                                                   renderojn::render::mix_chart(compatible_chart, samples, mode, mode == renderojn::render::SchedulingMode::Realtime,
                                                                                consumer, diagnostics, options.tracks);
                                               });
    return destination;
}

// Renders every .ojn in a folder, one after another.  A chart that fails does
// not stop the rest; the exit code reports whether all of them succeeded.
int run_batch(renderojn::app::Options options, renderojn::Diagnostics& diagnostics) {
    namespace fs = std::filesystem;
    const auto inputs = renderojn::app::collect_batch_inputs(options.input);
    if (inputs.empty()) {
        throw renderojn::Error(renderojn::ExitCode::Runtime, "No .ojn files in " + renderojn::io::path_to_utf8(options.input));
    }
    if (!options.outdir) options.outdir = options.input / "render";
    fs::create_directories(*options.outdir);
    print_warnings(diagnostics);

    std::size_t failed = 0;
    for (std::size_t index = 0; index < inputs.size(); ++index) {
        const auto& input = inputs[index];
        std::cout << "[" << (index + 1) << "/" << inputs.size() << "] " << renderojn::io::path_to_utf8(input.filename()) << '\n';
        renderojn::Diagnostics per_file;
        try {
            const auto destination = render_one(options, input, per_file);
            std::cout << "  -> " << renderojn::io::path_to_utf8(destination) << '\n';
            print_warnings(per_file, "  ");
        } catch (const std::exception& error) {
            print_warnings(per_file, "  ");
            std::cerr << "  error: " << error.what() << '\n';
            ++failed;
        }
    }
    std::cout << "Rendered " << (inputs.size() - failed) << " of " << inputs.size() << " file(s)";
    if (failed != 0) std::cout << "; " << failed << " failed";
    std::cout << '\n';
    return static_cast<int>(failed == 0 ? renderojn::ExitCode::Success : renderojn::ExitCode::Runtime);
}

// The whole program, on UTF-8 arguments.  The platform entry points below only
// exist to hand it correctly decoded text.
int run(const std::vector<std::string>& arguments) {
    // Warnings accumulated before a failure explain that failure (for example a
    // sample package silently resolved from the current directory), so the
    // diagnostics must outlive the try block and be flushed on every exit path.
    renderojn::Diagnostics diagnostics;
    try {
        auto options = renderojn::app::parse_cli(arguments);
        if (options.help) {
            std::cout << renderojn::app::banner() << '\n' << renderojn::app::usage();
            return static_cast<int>(renderojn::ExitCode::Success);
        }
        const bool batch = std::filesystem::is_directory(options.input);
        renderojn::app::validate_output_options(options, batch);
        renderojn::app::collect_play_warnings(options, diagnostics);
        if (!options.play && options.output_format == renderojn::app::OutputFormat::Wav) diagnostics.warn("WAV ignores --quality");
        std::cout << renderojn::app::banner();

        if (batch) return run_batch(options, diagnostics);

        if (options.outdir) std::filesystem::create_directories(*options.outdir);
        render_one(options, options.input, diagnostics);
        print_warnings(diagnostics);
        return static_cast<int>(renderojn::ExitCode::Success);
    } catch (const renderojn::Error& error) {
        print_warnings(diagnostics);
        std::cerr << "error: " << error.what() << '\n';
        return static_cast<int>(error.code());
    } catch (const std::exception& error) {
        print_warnings(diagnostics);
        std::cerr << "error: " << error.what() << '\n';
        return static_cast<int>(renderojn::ExitCode::Runtime);
    }
}

#ifdef _WIN32
std::string wide_to_utf8(const wchar_t* text) {
    const auto length = ::WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1) return {};
    std::string result(static_cast<std::size_t>(length - 1), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), length, nullptr, nullptr);
    return result;
}
#endif

} // namespace

#ifdef _WIN32
// The narrow main() would receive argv through the active code page, which
// cannot represent a Korean or Japanese path on a Western locale (and vice
// versa).  Take the wide command line instead and speak UTF-8 from here on;
// the console is switched to UTF-8 so titles and paths print back intact.
int wmain(int argc, wchar_t** argv) {
    ::SetConsoleOutputCP(CP_UTF8);
    std::vector<std::string> arguments;
    for (int index = 1; index < argc; ++index) arguments.push_back(wide_to_utf8(argv[index]));
    return run(arguments);
}
#else
int main(int argc, char** argv) {
    std::vector<std::string> arguments;
    for (int index = 1; index < argc; ++index) arguments.emplace_back(argv[index]);
    return run(arguments);
}
#endif
