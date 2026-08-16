#include "app/Cli.hpp"
#include "app/Version.hpp"
#include "core/io/Path.hpp"

#include <algorithm>
#include <array>
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

namespace {

std::string format_extension(OutputFormat format) {
    return format == OutputFormat::Wav ? ".wav" : format == OutputFormat::Mp3 ? ".mp3" : ".ogg";
}

// Windows treats these names (with or without an extension, any case) as
// devices, so a file cannot be created with one -- and Windows reserves the
// name even when an extension follows it (NUL.txt is still the NUL device), so
// only the part before the first dot is compared.  The superscript-digit COM/LPT
// forms are documented as reserved too; they are the UTF-8 bytes for U+00B9,
// U+00B2, U+00B3 (see Microsoft's file-naming rules).
bool is_reserved_device_name(const std::string& name) {
    // Names reserved on their own; bare COM/LPT are NOT reserved (only COMn/LPTn).
    static const std::array<const char*, 6> kExact{{"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$"}};
    static const std::array<const char*, 2> kNumbered{{"COM", "LPT"}};
    // The device name is what precedes the first dot (NUL.txt is still NUL).
    auto stem = name.substr(0, name.find('.'));
    std::string upper;
    upper.reserve(stem.size());
    for (const auto character : stem) upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(character))));

    for (const auto* exact : kExact) {
        if (upper == exact) return true;
    }
    // COM/LPT are reserved only when followed by a single 1-9, or one of the
    // superscript digits U+00B9/B2/B3 (Microsoft's rules).  COM0/LPT0 are not
    // reserved; a Windows probe creates them fine.
    for (const auto* prefix : kNumbered) {
        const std::string device = prefix;
        if (upper.size() <= device.size() || upper.compare(0, device.size(), device) != 0) continue;
        const auto tail = upper.substr(device.size());
        const bool ascii_digit = tail.size() == 1 && tail[0] >= '1' && tail[0] <= '9';
        const bool superscript = tail == "\xC2\xB9" || tail == "\xC2\xB2" || tail == "\xC2\xB3";  // U+00B9/B2/B3
        if (ascii_digit || superscript) return true;
    }
    return false;
}

} // namespace

void validate_output_options(const Options& options, bool input_is_directory) {
    if (options.output && options.outdir) {
        usage_error("--outfile and --outdir cannot be combined; --outfile names the whole path, --outdir only the folder");
    }
    if (input_is_directory && options.output) usage_error("--outfile cannot be used with a folder input; use --outdir");
    if (input_is_directory && options.sample_package) {
        usage_error("--sample-package cannot be used with a folder input; each chart names its own package");
    }
    if (input_is_directory && options.play) usage_error("--play needs a single .ojn file, not a folder");
    // The --outfile/--format extension conflict needs no I/O, so surface it here
    // before anything is read rather than deep inside the render.
    if (options.output && options.output->has_extension()) {
        const auto extension = format_extension(options.output_format);
        if (options.output->extension() != std::filesystem::path(extension)) {
            usage_error("--outfile extension conflicts with --format (expected " + extension + ")");
        }
    }
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
    // A Windows device name (CON, NUL, COM1...) cannot be a filename even with an
    // extension appended, so prefix it out of the reserved set.
    if (is_reserved_device_name(result)) result.insert(result.begin(), '_');
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
    const std::string extension = format_extension(options.output_format);
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

std::filesystem::path next_available_destination(const std::filesystem::path& destination) {
    // fs::exists asks the filesystem, which applies its own case and Unicode
    // rules for this volume -- so "Song.mp3"/"song.mp3", or "Élan"/"élan", alias
    // on a case-insensitive volume and are distinct on a case-sensitive one,
    // with no platform guesswork.
    std::error_code error;
    if (!std::filesystem::exists(destination, error)) return destination;

    const auto directory = destination.parent_path();
    const auto stem = destination.stem();
    const auto extension = destination.extension();
    for (int suffix = 2;; ++suffix) {
        auto candidate_name = stem;
        candidate_name += " (" + std::to_string(suffix) + ")";
        candidate_name += extension;
        const auto candidate = directory / candidate_name;
        if (!std::filesystem::exists(candidate, error)) return candidate;
    }
}

std::string banner() {
    return std::string("RenderOJN v") + version::kVersion + "\n" + version::kProjectUrl + "\n";
}

std::string usage() {
    return "Usage: RenderOJN <input.ojn | folder> [--difficulty e|n|h]\n"
           "                 [--tracks all|keysounds|background]\n"
           "                 [--format wav|mp3|ogg] [--outfile <path>] [--outdir <folder>]\n"
           "                 [--title-as-filename] [--no-cover-art] [--quality 1|2|3]\n"
           "                 [--sample-package <path>] [--play] [--help]\n"
           "\nDefaults: difficulty h, tracks all, format mp3, quality 3.\n"
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
    if (options.outdir) diagnostics.warn("--outdir is ignored by --play");
    if (options.title_as_filename) diagnostics.warn("--title-as-filename is ignored by --play");
    if (!options.cover_art) diagnostics.warn("--no-cover-art is ignored by --play");
    if (options.quality_set) diagnostics.warn("--quality is ignored by --play");
    if (options.tracks != render::TrackSelection::All) diagnostics.warn("--tracks is ignored by --play; all notes are played");
}

} // namespace renderojn::app
