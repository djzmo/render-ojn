#pragma once

#include "core/Diagnostic.hpp"
#include "core/format/Types.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace renderojn::app {

enum class OutputFormat { Wav, Mp3, Ogg };
enum class RenderMode { Quick, Realtime };

struct Options {
    std::filesystem::path input;
    format::Difficulty difficulty{format::Difficulty::Hard};
    RenderMode render_mode{RenderMode::Quick};
    OutputFormat output_format{OutputFormat::Mp3};
    int quality{3};
    bool play{};
    bool help{};
    bool format_set{};
    bool output_set{};
    bool quality_set{};
    bool render_mode_set{};
    std::optional<std::filesystem::path> output;
    std::optional<std::filesystem::path> sample_package;
};

[[nodiscard]] Options parse_cli(const std::vector<std::string>& arguments);
[[nodiscard]] std::filesystem::path resolve_output_path(const Options& options);
[[nodiscard]] std::string usage();
[[nodiscard]] std::string banner();
void collect_play_warnings(const Options& options, Diagnostics& diagnostics);

} // namespace renderojn::app
