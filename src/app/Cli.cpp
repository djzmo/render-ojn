#include "app/Cli.hpp"
#include "app/Version.hpp"
#include "core/io/Path.hpp"

#include <algorithm>
#include <cctype>
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
            result.output = io::utf8_to_path(take_value(arguments, index, token));
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
            result.sample_package = io::utf8_to_path(take_value(arguments, index, token));
            continue;
        }
        if (!positional_only && token == "--outdir") {
            result.outdir = io::utf8_to_path(take_value(arguments, index, token));
            continue;
        }
        if (!positional_only && token == "--title-as-filename") {
            result.title_as_filename = true;
            continue;
        }
        if (!positional_only && token == "--no-cover-art") {
            result.cover_art = false;
            continue;
        }
        if (!positional_only && token.rfind("--", 0) == 0) usage_error("Unknown option: " + token);
        if (!result.input.empty()) {
            usage_error("Only one input may be supplied (got '" + io::path_to_utf8(result.input) + "' and '" + token +
                        "'). Quote a path that contains spaces: RenderOJN \"C:\\My Songs\\o2ma100.ojn\"");
        }
        result.input = io::utf8_to_path(token);
    }
    if (result.input.empty()) usage_error("Missing input .ojn file");
    return result;
}

void validate_output_options(const Options& options, bool input_is_directory) {
    if (options.output && options.outdir) {
        usage_error("--outfile and --outdir cannot be combined; --outfile names the whole path, --outdir only the folder");
    }
    if (input_is_directory && options.output) usage_error("--outfile cannot be used with a folder input; use --outdir");
    if (input_is_directory && options.play) usage_error("--play needs a single .ojn file, not a folder");
}

std::string sanitize_filename(std::string_view utf8) {
    std::string result;
    result.reserve(utf8.size());
    for (const auto character : utf8) {
        const auto byte = static_cast<unsigned char>(character);
        const bool illegal = byte < 0x20 || character == '\\' || character == '/' || character == ':' || character == '*' ||
                             character == '?' || character == '"' || character == '<' || character == '>' || character == '|';
        result.push_back(illegal ? '_' : character);
    }
    // Windows drops trailing spaces and dots from names; leading spaces are
    // merely confusing.
    const auto first = result.find_first_not_of(' ');
    if (first == std::string::npos) return {};
    result.erase(0, first);
    const auto last = result.find_last_not_of(" .");
    if (last == std::string::npos) return {};
    result.erase(last + 1);
    return result;
}

const char* tracks_suffix(render::TrackSelection tracks) {
    switch (tracks) {
    case render::TrackSelection::Keysounds: return "_keysounds";
    case render::TrackSelection::Background: return "_background";
    default: return "";
    }
}

std::filesystem::path input_stem(const std::filesystem::path& input) {
    const auto name = input.filename();
    auto extension = io::path_to_utf8(name.extension());
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return extension == ".ojn" ? name.stem() : name;
}

std::filesystem::path resolve_output_path(const Options& options, const std::filesystem::path& input,
                                          const std::string& title_utf8) {
    const std::string extension = options.output_format == OutputFormat::Wav ? ".wav" : options.output_format == OutputFormat::Mp3 ? ".mp3" : ".ogg";
    if (options.output) {
        auto output = *options.output;
        if (!output.has_extension()) {
            output += extension;
            return output;
        }
        if (output.extension() != std::filesystem::path(extension)) {
            usage_error("--outfile extension conflicts with --format (expected " + extension + ")");
        }
        return output;
    }
    std::filesystem::path stem;
    if (options.title_as_filename) {
        const auto title = sanitize_filename(title_utf8);
        if (!title.empty()) stem = io::utf8_to_path(title);
    }
    if (stem.empty()) stem = input_stem(input);
    stem += tracks_suffix(options.tracks);
    stem += extension;
    const auto directory = options.outdir ? *options.outdir : std::filesystem::absolute(input).parent_path();
    return directory / stem;
}

std::vector<std::filesystem::path> collect_batch_inputs(const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> inputs;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file()) continue;
        // input_stem strips only a ".ojn" extension, so a changed name means it had one.
        if (input_stem(entry.path()) == entry.path().filename()) continue;
        inputs.push_back(entry.path());
    }
    std::sort(inputs.begin(), inputs.end(), [](const std::filesystem::path& left, const std::filesystem::path& right) {
        return io::path_to_utf8(left.filename()) < io::path_to_utf8(right.filename());
    });
    return inputs;
}

std::string banner() {
    return std::string("RenderOJN v") + version::kVersion + "\n" + version::kProjectUrl + "\n";
}

std::string usage() {
    return "Usage: RenderOJN <input.ojn | folder> [--difficulty e|n|h] [--rendermode quick|realtime]\n"
           "                 [--tracks all|keysounds|background]\n"
           "                 [--format wav|mp3|ogg] [--outfile <path>] [--outdir <folder>]\n"
           "                 [--title-as-filename] [--no-cover-art] [--quality 1|2|3]\n"
           "                 [--sample-package <path>] [--play] [--help]\n"
           "\nDefaults: difficulty h, rendermode quick, tracks all, format mp3, quality 3.\n"
           "\n--tracks selects which notes to sound: keysounds are the playable lanes,\n"
           "background is the autoplay/BGM stream. All modes keep the same length.\n"
           "\nOutput is named <input stem>[_keysounds|_background].<format> beside the\n"
           "input, or in --outdir. --title-as-filename uses the chart's title instead\n"
           "of the input stem. --outfile names the file itself (the extension is\n"
           "optional and follows --format). A folder input renders every .ojn in it,\n"
           "into <folder>/render unless --outdir is given.\n"
           "\nMP3 and Ogg carry the chart's title, artist and cover art; --no-cover-art\n"
           "leaves the picture out.\n";
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
