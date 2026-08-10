#include "app/Cli.hpp"
#include "app/Version.hpp"
#include "core/Diagnostic.hpp"
#include "core/audio/Decoder.hpp"
#include "core/audio/Playback.hpp"
#include "core/compat/CompatibilityProfile.hpp"
#include "core/format/OjnParser.hpp"
#include "core/format/PackageParser.hpp"
#include "core/io/ByteReader.hpp"
#include "core/output/Encoder.hpp"
#include "core/render/Mixer.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr std::uint64_t kTwoGiB = 2ULL * 1024ULL * 1024ULL * 1024ULL;

std::filesystem::path resolve_sample_package(const renderojn::app::Options& options, const renderojn::format::OjnHeader& header,
                                             renderojn::Diagnostics& diagnostics) {
    namespace fs = std::filesystem;
    if (options.sample_package) {
        if (!fs::is_regular_file(*options.sample_package)) {
            throw renderojn::Error(renderojn::ExitCode::Runtime, "Sample-package override does not exist: " + options.sample_package->string());
        }
        return *options.sample_package;
    }
    if (header.package_name.empty()) {
        throw renderojn::Error(renderojn::ExitCode::Runtime, "OJN does not name a sample package; pass --sample-package <path>");
    }
    const auto beside_input = options.input.parent_path() / fs::path(header.package_name);
    if (fs::is_regular_file(beside_input)) return beside_input;
    const auto current_directory = fs::current_path() / fs::path(header.package_name);
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

std::string genre_for(std::uint32_t value, renderojn::Diagnostics& diagnostics) {
    static const std::vector<std::string> genres{"Ballad", "Rock", "Dance", "Techno", "Hip-hop", "Soul/R&B", "Jazz", "Funk", "Classical", "Traditional", "Etc"};
    if (value >= genres.size()) {
        diagnostics.warn("invalid genre code; using Etc");
        return "Etc";
    }
    return genres[value];
}

void print_warnings(const renderojn::Diagnostics& diagnostics) {
    for (const auto& warning : diagnostics.warnings()) std::cerr << "warning: " << warning << '\n';
}

} // namespace

int main(int argc, char** argv) {
    // Warnings accumulated before a failure explain that failure (for example a
    // sample package silently resolved from the current directory), so the
    // diagnostics must outlive the try block and be flushed on every exit path.
    renderojn::Diagnostics diagnostics;
    try {
        std::vector<std::string> arguments;
        for (int index = 1; index < argc; ++index) arguments.emplace_back(argv[index]);
        auto options = renderojn::app::parse_cli(arguments);
        if (options.help) {
            std::cout << renderojn::app::banner() << '\n' << renderojn::app::usage();
            return static_cast<int>(renderojn::ExitCode::Success);
        }

        renderojn::app::collect_play_warnings(options, diagnostics);
        if (!options.play && options.output_format == renderojn::app::OutputFormat::Wav) diagnostics.warn("WAV ignores --quality");
            const auto destination = options.play ? std::filesystem::path{} : renderojn::app::resolve_output_path(options);
        std::cout << renderojn::app::banner();

        const auto ojn = renderojn::io::read_file(options.input.string(), kTwoGiB, "OJN");
        // parse_ojn_chart parses and validates the header itself, so reuse the copy
        // it returns instead of running the whole header validation twice.
        const auto chart = renderojn::format::parse_ojn_chart(ojn, options.difficulty);
        const auto& header = chart.header;
        const auto package_path = resolve_sample_package(options, header, diagnostics);
        const auto package_data = renderojn::io::read_file(package_path.string(), kTwoGiB, "sample package");
        auto compatible_chart = chart;
        renderojn::compat::apply_known_ojn_ojm_profiles(compatible_chart, *ojn, *package_data, diagnostics);
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
        } else {
            const renderojn::output::Tags tags{header.title, header.artist, header.song_id, genre_for(header.genre, diagnostics),
                                                std::string("Generated by RenderOJN v") + renderojn::app::version::kVersion + "\n" + renderojn::app::version::kProjectUrl};
            const auto mode = map_mode(options.render_mode);
            renderojn::output::encode_transactionally(map_format(options.output_format), destination, renderojn::render::output_frame_count(compatible_chart),
                                                       options.quality, tags,
                                                       [&](const auto& consumer) {
                                                           renderojn::render::mix_chart(compatible_chart, samples, mode, mode == renderojn::render::SchedulingMode::Realtime,
                                                                                        consumer, diagnostics);
                                                       });
        }
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
