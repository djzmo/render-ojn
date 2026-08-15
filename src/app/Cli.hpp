#pragma once

#include "core/Diagnostic.hpp"
#include "core/format/Types.hpp"
#include "core/render/Mixer.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace renderojn::app {

enum class OutputFormat { Wav, Mp3, Ogg };
enum class RenderMode { Quick, Realtime };

struct Options {
    std::filesystem::path input;
    format::Difficulty difficulty{format::Difficulty::Hard};
    RenderMode render_mode{RenderMode::Quick};
    OutputFormat output_format{OutputFormat::Mp3};
    render::TrackSelection tracks{render::TrackSelection::All};
    int quality{3};
    bool play{};
    bool help{};
    bool format_set{};
    bool output_set{};
    bool quality_set{};
    bool render_mode_set{};
    std::optional<std::filesystem::path> output;
    std::optional<std::filesystem::path> outdir;
    std::optional<std::filesystem::path> sample_package;
    bool title_as_filename{};
    bool cover_art{true};
};

[[nodiscard]] Options parse_cli(const std::vector<std::string>& arguments);

// Usage errors that need no file I/O to detect: they run right after parsing so
// a bad combination fails before anything is read or written.
void validate_output_options(const Options& options, bool input_is_directory);

// Replaces the characters no filesystem accepts (\ / : * ? " < > | and C0
// controls) with '_' and trims the spaces and dots Windows will not keep at the
// ends.  May return an empty string.
[[nodiscard]] std::string sanitize_filename(std::string_view utf8);

// "", "_keysounds" or "_background": the --tracks value itself, so a stem
// render never overwrites the full mix beside it.
[[nodiscard]] const char* tracks_suffix(render::TrackSelection tracks);

// The input's filename with a trailing ".ojn" (any case) removed; any other
// extension is kept, since it is part of the name.
[[nodiscard]] std::filesystem::path input_stem(const std::filesystem::path& input);

// Where one render lands.  --outfile wins verbatim; otherwise the name is
// <stem><tracks suffix>.<ext> in --outdir or beside the input, where the stem is
// the sanitized title under --title-as-filename (falling back to the input's
// stem when the title sanitizes to nothing) and the input's stem otherwise.
[[nodiscard]] std::filesystem::path resolve_output_path(const Options& options, const std::filesystem::path& input,
                                                        const std::string& title_utf8);
// The .ojn files (any case) directly inside a folder, sorted by name; nested
// folders are not searched.
[[nodiscard]] std::vector<std::filesystem::path> collect_batch_inputs(const std::filesystem::path& directory);

[[nodiscard]] std::string usage();
[[nodiscard]] std::string banner();
void collect_play_warnings(const Options& options, Diagnostics& diagnostics);

} // namespace renderojn::app
