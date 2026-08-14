#include "app/Cli.hpp"
#include "app/Version.hpp"

#include <algorithm>
#include <sstream>

namespace renderojn::app {
namespace {

[[noreturn]] void usage_error(const std::string& text) {
    throw Error(ExitCode::Usage, text + "\n\n" + usage());
}

std::string take_value(const std::vector<std::string>& arguments, std::size_t& index, const std::string& option) {
    if (++index >= arguments.size() || arguments[index].empty()) {
        usage_error("Missing value for " + option);
    }
    return arguments[index];
}

OutputFormat parse_format(const std::string& value) {
    if (value == "wav") return OutputFormat::Wav;
    if (value == "mp3") return OutputFormat::Mp3;
    if (value == "ogg") return OutputFormat::Ogg;
    usage_error("Invalid --format value: " + value);
}

} // namespace

Options parse_cli(const std::vector<std::string>& arguments) {
    if (std::find(arguments.begin(), arguments.end(), "--help") != arguments.end()) {
        Options help;
        help.help = true;
        return help;
    }
    Options result;
    bool positional_only{};
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const auto& token = arguments[index];
        if (!positional_only && token == "--") {
            positional_only = true;
            continue;
        }
        if (!positional_only && token == "--play") {
            result.play = true;
            continue;
        }
        if (!positional_only && token == "--difficulty") {
            const auto value = take_value(arguments, index, token);
            if (value == "e") result.difficulty = format::Difficulty::Easy;
            else if (value == "n") result.difficulty = format::Difficulty::Normal;
            else if (value == "h") result.difficulty = format::Difficulty::Hard;
            else usage_error("Invalid --difficulty value: " + value);
            continue;
        }
        if (!positional_only && token == "--tracks") {
            const auto value = take_value(arguments, index, token);
            if (value == "all") result.tracks = render::TrackSelection::All;
            else if (value == "keysounds") result.tracks = render::TrackSelection::Keysounds;
            else if (value == "background") result.tracks = render::TrackSelection::Background;
            else usage_error("Invalid --tracks value: " + value);
            continue;
        }
        if (!positional_only && token == "--rendermode") {
            const auto value = take_value(arguments, index, token);
            if (value == "quick") result.render_mode = RenderMode::Quick;
            else if (value == "realtime") result.render_mode = RenderMode::Realtime;
            else usage_error("Invalid --rendermode value: " + value);
            result.render_mode_set = true;
            continue;
        }
        if (!positional_only && token == "--format") {
            result.output_format = parse_format(take_value(arguments, index, token));
            result.format_set = true;
            continue;
        }
        if (!positional_only && token == "--outfile") {
            result.output = std::filesystem::path(take_value(arguments, index, token));
            result.output_set = true;
            continue;
        }
        if (!positional_only && token == "--quality") {
            const auto value = take_value(arguments, index, token);
            if (value.size() != 1 || value[0] < '1' || value[0] > '3') usage_error("Invalid --quality value: " + value);
            result.quality = value[0] - '0';
            result.quality_set = true;
            continue;
        }
        if (!positional_only && token == "--sample-package") {
            result.sample_package = std::filesystem::path(take_value(arguments, index, token));
            continue;
        }
        if (!positional_only && token.rfind("--", 0) == 0) usage_error("Unknown option: " + token);
        if (!result.input.empty()) usage_error("Only one input .ojn file may be supplied");
        result.input = token;
    }
    if (result.input.empty()) usage_error("Missing input .ojn file");
    return result;
}

std::filesystem::path resolve_output_path(const Options& options) {
    const std::string extension = options.output_format == OutputFormat::Wav ? ".wav" : options.output_format == OutputFormat::Mp3 ? ".mp3" : ".ogg";
    if (!options.output) return std::filesystem::absolute(options.input).string() + extension;
    auto output = *options.output;
    if (!output.has_extension()) return output.string() + extension;
    if (output.extension().string() != extension) {
        usage_error("--outfile extension conflicts with --format (expected " + extension + ")");
    }
    return output;
}

std::string banner() {
    return std::string("RenderOJN v") + version::kVersion + "\n" + version::kProjectUrl + "\n";
}

std::string usage() {
    return "Usage: RenderOJN <input.ojn> [--difficulty e|n|h] [--rendermode quick|realtime]\n"
           "                 [--tracks all|keysounds|background]\n"
           "                 [--format wav|mp3|ogg] [--outfile <path>] [--quality 1|2|3]\n"
           "                 [--sample-package <path>] [--play] [--help]\n"
           "\nDefaults: difficulty h, rendermode quick, tracks all, format mp3, quality 3.\n"
           "\n--tracks selects which notes to sound: keysounds are the playable lanes,\n"
           "background is the autoplay/BGM stream. All modes keep the same length.\n";
}

void collect_play_warnings(const Options& options, Diagnostics& diagnostics) {
    if (!options.play) return;
    if (options.format_set) diagnostics.warn("--format is ignored by --play");
    if (options.output_set) diagnostics.warn("--outfile is ignored by --play");
    if (options.quality_set) diagnostics.warn("--quality is ignored by --play");
    if (options.tracks != render::TrackSelection::All) diagnostics.warn("--tracks is ignored by --play; all notes are played");
    if (options.render_mode_set) diagnostics.warn("--rendermode is ignored by --play; realtime playback is used");
}

} // namespace renderojn::app
