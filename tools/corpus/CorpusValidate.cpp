// Opt-in corpus validation harness.
//
// This is a private verification tool, not a public API and not an installed
// target.  It inventories a real O2Jam corpus, parses every OJN at all three
// difficulties -- ordinary and Korea-era `new` wrappers alike, since 1.0.1
// decrypts the latter -- parses and decodes every sample package once, and
// censuses the timing constructs each chart uses.
//
// It never mutates, copies, or packages any source asset; it only reads them
// and writes a report to a caller-provided directory.

#include "core/Diagnostic.hpp"
#include "core/audio/Decoder.hpp"
#include "core/format/OjnParser.hpp"
#include "core/format/PackageParser.hpp"
#include "core/io/ByteReader.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr std::uint64_t kTwoGiB = 2ULL * 1024ULL * 1024ULL * 1024ULL;

struct Options {
    std::vector<std::filesystem::path> roots;
    std::filesystem::path report_directory;
    bool decode_samples{true};
};

struct Totals {
    std::size_t charts_ordinary{};
    // Retained at zero since 1.0.1 decrypts `new` wrappers instead of skipping
    // them.  The field stays in the report so a 1.0.0 report and a 1.0.1 report
    // remain directly comparable rather than differing by a missing key.
    std::size_t charts_new_skipped{};
    std::size_t charts_failed{};
    std::size_t packages_parsed{};
    std::size_t packages_failed{};
    std::size_t packages_decoded{};
    std::size_t decode_failed{};
    // Census of the timing constructs the two-pass normalizer exists to handle.
    // Synthetic fixtures prove the code is correct; only a real-corpus count
    // shows whether shipped charts actually exercise it.
    std::size_t charts_with_measure_fractions{};
    std::size_t charts_with_tempo_changes{};
    std::size_t charts_with_indivisible_subdivisions{};
    std::size_t total_measure_fraction_events{};
    std::size_t total_tempo_events{};
    std::map<std::string, std::size_t> package_kinds;
    std::map<std::string, std::size_t> failure_reasons;
    std::map<std::string, std::size_t> decode_warnings;
};

// Counts channel-0 measure-fraction events, channel-1 tempo events, and event
// sets whose subdivision does not divide 192, read straight from the chart
// section.  This deliberately re-reads raw bytes instead of using parsed notes,
// because the parser normalizes these constructs away by design.
struct TimingCensus {
    std::size_t measure_fractions{};
    std::size_t tempo_events{};
    std::size_t indivisible_subdivisions{};
};

TimingCensus census_timing_events(const std::vector<std::uint8_t>& bytes, std::uint32_t start, std::uint32_t finish) {
    TimingCensus census;
    if (finish > bytes.size() || start > finish) return census;
    const auto read_u32 = [&bytes](std::size_t at) {
        return static_cast<std::uint32_t>(bytes[at]) | (static_cast<std::uint32_t>(bytes[at + 1]) << 8U) |
               (static_cast<std::uint32_t>(bytes[at + 2]) << 16U) | (static_cast<std::uint32_t>(bytes[at + 3]) << 24U);
    };
    const auto read_u16 = [&bytes](std::size_t at) {
        return static_cast<std::uint32_t>(bytes[at]) | (static_cast<std::uint32_t>(bytes[at + 1]) << 8U);
    };
    std::size_t position = start;
    while (position + 8 <= finish) {
        const auto channel = read_u16(position + 4);
        const auto count = read_u16(position + 6);
        position += 8;
        const std::size_t payload = count * 4U;
        if (position + payload > finish) break;
        if (count != 0 && 192U % count != 0U) ++census.indivisible_subdivisions;
        if (channel <= 1) {
            for (std::uint32_t index = 0; index < count; ++index) {
                const auto raw = read_u32(position + static_cast<std::size_t>(index) * 4U);
                float value{};
                std::memcpy(&value, &raw, sizeof(value));
                if (value == 0.0F) continue; // zero is padding in both timing channels
                if (channel == 0) ++census.measure_fractions;
                else ++census.tempo_events;
            }
        }
        position += payload;
    }
    return census;
}

[[noreturn]] void usage_error(const std::string& message) {
    throw renderojn::Error(renderojn::ExitCode::Usage, message);
}

std::string json_escape(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (const char character : value) {
        switch (character) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(character) < 0x20) {
                std::ostringstream escaped;
                escaped << "\\u" << std::hex << std::uppercase;
                escaped.width(4);
                escaped.fill('0');
                escaped << static_cast<int>(static_cast<unsigned char>(character));
                out += escaped.str();
            } else {
                out += character;
            }
        }
    }
    return out;
}

// Case IDs stay root-relative so identical filenames under different roots
// remain distinct cases instead of silently collapsing into one.  The
// root-relative path alone is NOT sufficient: two roots routinely hold the same
// filenames (99 of 100 O2Jam Thai charts share a name with an O2Jam chart), and
// even the root's leaf directory name can repeat across roots (both
// `e-Games/O2Jam` and `NOWCOM/O2Jam` have the leaf `O2Jam`).  Prefixing the
// caller's root index is what actually makes the ID unique, whatever the
// directories are named.
std::string case_id(std::size_t root_index, const std::filesystem::path& root, const std::filesystem::path& file) {
    std::error_code failed;
    auto relative = std::filesystem::relative(file, root, failed);
    auto text = failed ? file.filename().string() : relative.generic_string();
    std::replace(text.begin(), text.end(), '\\', '/');
    return std::to_string(root_index) + '#' + text;
}

std::string describe_kind(renderojn::format::PackageKind kind) {
    switch (kind) {
    case renderojn::format::PackageKind::M30: return "M30";
    case renderojn::format::PackageKind::OMC: return "OMC";
    case renderojn::format::PackageKind::OJM: return "OJM";
    }
    return "unknown";
}

// The M30 encryption flag lives at a fixed header offset.  Reading it directly
// keeps the report informative without widening the parser's public surface.
std::string describe_package_detail(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() >= 12 && std::equal(bytes.begin(), bytes.begin() + 3, "M30")) {
        const std::uint32_t flag = static_cast<std::uint32_t>(bytes[8]) |
                                   (static_cast<std::uint32_t>(bytes[9]) << 8U) |
                                   (static_cast<std::uint32_t>(bytes[10]) << 16U) |
                                   (static_cast<std::uint32_t>(bytes[11]) << 24U);
        return "flag " + std::to_string(flag);
    }
    return {};
}

Options parse_options(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        const auto next = [&]() -> std::string {
            if (index + 1 >= argc) usage_error("Missing value for " + argument);
            return argv[++index];
        };
        if (argument == "--root") {
            options.roots.emplace_back(next());
        } else if (argument == "--report-directory") {
            options.report_directory = next();
        } else if (argument == "--no-decode") {
            options.decode_samples = false;
        } else if (argument == "--help") {
            std::cout << "Usage: renderojn_corpus_validate --root <dir> [--root <dir>...]\n"
                      << "                                 --report-directory <dir> [--no-decode]\n";
            std::exit(0);
        } else {
            usage_error("Unknown argument: " + argument);
        }
    }
    if (options.roots.empty()) usage_error("At least one --root is required");
    if (options.report_directory.empty()) usage_error("--report-directory is required");
    return options;
}

class Report {
public:
    explicit Report(const std::filesystem::path& directory)
        : path_(directory / "corpus-cases.jsonl"), stream_(path_, std::ios::binary | std::ios::trunc) {
        if (!stream_) {
            throw renderojn::Error(renderojn::ExitCode::Runtime, "Unable to write report: " + path_.string());
        }
    }

    // Records are flushed per case so an interrupted run still leaves a usable
    // partial report on disk.
    void record(const std::string& id, const std::string& category, const std::string& kind,
                const std::string& detail, const std::string& message) {
        stream_ << "{\"case\":\"" << json_escape(id) << "\",\"category\":\"" << json_escape(category) << "\""
                << ",\"kind\":\"" << json_escape(kind) << "\""
                << ",\"detail\":\"" << json_escape(detail) << "\""
                << ",\"message\":\"" << json_escape(message) << "\"}\n";
        stream_.flush();
    }

private:
    std::filesystem::path path_;
    std::ofstream stream_;
};

void write_summary(const std::filesystem::path& directory, const Totals& totals) {
    // Written to a temporary path and renamed so an interrupted run can never
    // leave a half-written summary behind.
    const auto final_path = directory / "corpus-summary.json";
    const auto temporary_path = directory / "corpus-summary.json.tmp";
    {
        std::ofstream stream(temporary_path, std::ios::binary | std::ios::trunc);
        if (!stream) {
            throw renderojn::Error(renderojn::ExitCode::Runtime, "Unable to write summary: " + temporary_path.string());
        }
        stream << "{\n  \"charts_ordinary\": " << totals.charts_ordinary
               << ",\n  \"charts_new_skipped\": " << totals.charts_new_skipped
               << ",\n  \"charts_failed\": " << totals.charts_failed
               << ",\n  \"packages_parsed\": " << totals.packages_parsed
               << ",\n  \"packages_failed\": " << totals.packages_failed
               << ",\n  \"packages_decoded\": " << totals.packages_decoded
               << ",\n  \"decode_failed\": " << totals.decode_failed
               << ",\n  \"charts_with_measure_fractions\": " << totals.charts_with_measure_fractions
               << ",\n  \"charts_with_tempo_changes\": " << totals.charts_with_tempo_changes
               << ",\n  \"charts_with_indivisible_subdivisions\": " << totals.charts_with_indivisible_subdivisions
               << ",\n  \"total_measure_fraction_events\": " << totals.total_measure_fraction_events
               << ",\n  \"total_tempo_events\": " << totals.total_tempo_events;
        stream << ",\n  \"package_kinds\": {";
        bool first = true;
        for (const auto& [key, value] : totals.package_kinds) {
            if (!first) stream << ",";
            first = false;
            stream << "\n    \"" << json_escape(key) << "\": " << value;
        }
        stream << "\n  }";
        stream << ",\n  \"failure_reasons\": {";
        first = true;
        for (const auto& [key, value] : totals.failure_reasons) {
            if (!first) stream << ",";
            first = false;
            stream << "\n    \"" << json_escape(key) << "\": " << value;
        }
        stream << "\n  }";
        stream << ",\n  \"decode_warnings\": {";
        first = true;
        for (const auto& [key, value] : totals.decode_warnings) {
            if (!first) stream << ",";
            first = false;
            stream << "\n    \"" << json_escape(key) << "\": " << value;
        }
        stream << "\n  }\n}\n";
        if (!stream) {
            throw renderojn::Error(renderojn::ExitCode::Runtime, "Unable to finalize summary: " + temporary_path.string());
        }
    }
    std::error_code failed;
    std::filesystem::rename(temporary_path, final_path, failed);
    if (failed) {
        throw renderojn::Error(renderojn::ExitCode::Runtime, "Unable to publish summary: " + final_path.string());
    }
}

void validate_chart(const std::string& id, const std::filesystem::path& file, Report& report, Totals& totals) {
    try {
        const auto buffer = renderojn::io::read_file(file, kTwoGiB, "OJN");
        // All three difficulties are parsed from the same immutable buffer.
        std::size_t total_notes = 0;
        for (const auto difficulty : {renderojn::format::Difficulty::Easy, renderojn::format::Difficulty::Normal,
                                      renderojn::format::Difficulty::Hard}) {
            const auto chart = renderojn::format::parse_ojn_chart(buffer, difficulty);
            total_notes += chart.notes.size();
        }
        ++totals.charts_ordinary;

        // Census the raw timing constructs across all three chart sections.  This
        // must read the NORMALIZED buffer: a Korea-era `new` wrapper is still
        // encrypted in `buffer`, while the header offsets below come from the
        // decrypted stream, so censusing the raw bytes would walk ciphertext and
        // report meaningless counts.
        const auto normalized = renderojn::format::normalize_ojn(buffer);
        const auto header = renderojn::format::parse_ojn_header(normalized);
        TimingCensus census;
        for (std::size_t index = 0; index < 3; ++index) {
            const auto section = census_timing_events(normalized->bytes(), header.chart_offsets[index],
                                                      header.chart_offsets[index + 1]);
            census.measure_fractions += section.measure_fractions;
            census.tempo_events += section.tempo_events;
            census.indivisible_subdivisions += section.indivisible_subdivisions;
        }
        if (census.measure_fractions != 0) ++totals.charts_with_measure_fractions;
        if (census.tempo_events != 0) ++totals.charts_with_tempo_changes;
        if (census.indivisible_subdivisions != 0) ++totals.charts_with_indivisible_subdivisions;
        totals.total_measure_fraction_events += census.measure_fractions;
        totals.total_tempo_events += census.tempo_events;

        report.record(id, "chart_ok", "OJN",
                      "notes " + std::to_string(total_notes) + " fractions " + std::to_string(census.measure_fractions) +
                          " tempos " + std::to_string(census.tempo_events) + " indivisible " +
                          std::to_string(census.indivisible_subdivisions),
                      "");
    } catch (const renderojn::Error& error) {
        // Every OJN in scope is now parseable, `new` wrappers included, so there
        // is no expected-skip class left: any error here is a genuine failure.
        // Matching on message text to reclassify one would also be fragile, since
        // a malformed `new` wrapper reports through the same wording as a
        // successfully rejected one.
        const std::string message = error.what();
        ++totals.charts_failed;
        ++totals.failure_reasons[message];
        report.record(id, "chart_failed", "OJN", "", message);
    }
}

void validate_package(const std::string& id, const std::filesystem::path& file, bool decode, Report& report,
                      Totals& totals) {
    std::string detail;
    try {
        const auto buffer = renderojn::io::read_file(file, kTwoGiB, "sample package");
        detail = describe_package_detail(buffer->bytes());
        const auto package = renderojn::format::parse_sample_package(buffer);
        ++totals.packages_parsed;
        const auto kind = describe_kind(package.kind);
        ++totals.package_kinds[detail.empty() ? kind : kind + " " + detail];
        if (!decode) {
            report.record(id, "package_parsed", kind, detail, "");
            return;
        }
        try {
            // Decoder warnings carry real compatibility signal, such as the
            // single-page Ogg CRC repair, so they are recorded per case.
            renderojn::Diagnostics diagnostics;
            const auto decoded = renderojn::audio::decode_samples(package, diagnostics);
            ++totals.packages_decoded;
            std::string message = "samples " + std::to_string(decoded.size());
            for (const auto& warning : diagnostics.warnings()) {
                ++totals.decode_warnings[warning];
                message += "; warning: " + warning;
            }
            report.record(id, "package_decoded", kind, detail, message);
        } catch (const renderojn::Error& error) {
            ++totals.decode_failed;
            ++totals.failure_reasons[error.what()];
            report.record(id, "package_decode_failed", kind, detail, error.what());
        }
    } catch (const renderojn::Error& error) {
        ++totals.packages_failed;
        ++totals.failure_reasons[error.what()];
        report.record(id, "package_failed", "", detail, error.what());
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        const auto options = parse_options(argc, argv);
        std::error_code failed;
        std::filesystem::create_directories(options.report_directory, failed);
        if (failed) {
            throw renderojn::Error(renderojn::ExitCode::Runtime,
                                   "Unable to create report directory: " + options.report_directory.string());
        }

        Report report(options.report_directory);
        Totals totals;

        for (std::size_t root_index = 0; root_index < options.roots.size(); ++root_index) {
            const auto& root = options.roots[root_index];
            if (!std::filesystem::is_directory(root)) {
                throw renderojn::Error(renderojn::ExitCode::Usage, "Not a directory: " + root.string());
            }
            std::error_code walk_failed;
            for (std::filesystem::recursive_directory_iterator iterator(
                     root, std::filesystem::directory_options::skip_permission_denied, walk_failed),
                 last;
                 iterator != last; iterator.increment(walk_failed)) {
                // A truncated walk must never look like a complete one: a partial
                // scan would report the same "charts: N ordinary" summary and exit
                // 0, silently overstating corpus coverage.
                if (walk_failed) {
                    throw renderojn::Error(renderojn::ExitCode::Runtime,
                                           "Directory scan of " + root.string() + " failed before completion: " +
                                               walk_failed.message());
                }
                if (!iterator->is_regular_file(failed)) continue;
                auto extension = iterator->path().extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(),
                               [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
                const auto id = case_id(root_index, root, iterator->path());
                if (extension == ".ojn") {
                    validate_chart(id, iterator->path(), report, totals);
                } else if (extension == ".ojm") {
                    validate_package(id, iterator->path(), options.decode_samples, report, totals);
                }
            }
        }

        write_summary(options.report_directory, totals);

        std::cout << "charts: " << totals.charts_ordinary << " ordinary, " << totals.charts_new_skipped
                  << " skipped (new), " << totals.charts_failed << " failed\n";
        std::cout << "packages: " << totals.packages_parsed << " parsed, " << totals.packages_decoded << " decoded, "
                  << totals.packages_failed << " failed, " << totals.decode_failed << " decode failures\n";
        for (const auto& [kind, count] : totals.package_kinds) {
            std::cout << "  " << kind << ": " << count << "\n";
        }
        std::cout << "timing constructs: " << totals.charts_with_measure_fractions << " chart(s) with measure fractions ("
                  << totals.total_measure_fraction_events << " events), " << totals.charts_with_tempo_changes
                  << " with tempo changes (" << totals.total_tempo_events << " events), "
                  << totals.charts_with_indivisible_subdivisions << " with subdivisions not dividing 192\n";
        // A corpus discovery must not be hidden behind a zero exit code.
        const bool clean = totals.charts_failed == 0 && totals.packages_failed == 0 && totals.decode_failed == 0;
        return clean ? 0 : 1;
    } catch (const renderojn::Error& error) {
        std::cerr << "error: " << error.what() << "\n";
        return static_cast<int>(error.code());
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
}
