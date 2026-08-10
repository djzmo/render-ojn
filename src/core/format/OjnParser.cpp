#include "core/format/OjnParser.hpp"

#include "core/Diagnostic.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <vector>

namespace renderojn::format {
namespace {

constexpr std::size_t kHeaderSize = 300;
constexpr std::uint32_t kOjnSignature = 0x006e6a6fU; // "ojn\\0" in little endian.

[[noreturn]] void malformed(const std::string& message) {
    throw Error(ExitCode::Runtime, "Malformed OJN: " + message);
}

std::uint64_t seconds_to_frame(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0) malformed("invalid event time");
    const double frames = seconds * static_cast<double>(kSampleRate);
    if (!std::isfinite(frames) || frames > static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
        malformed("event time overflows frame counter");
    }
    return static_cast<std::uint64_t>(std::llround(frames));
}

enum class RawKind : std::uint8_t { MeasureFraction, Tempo, Note };

struct RawEvent {
    RawKind kind{};
    std::uint32_t measure{};
    std::uint16_t slot_index{};
    std::uint16_t slot_count{};
    std::uint16_t reference_id{};
    std::uint8_t note_type{};
    float scalar_value{};
    std::uint64_t ordinal{};
};

struct TimelineEvent {
    RawEvent event;
    double position{};
};

struct PrefixAdjustment {
    std::uint32_t measure{};
    double total{};
};

double effective_position(const RawEvent& event, const std::vector<PrefixAdjustment>& adjustments) {
    if (event.slot_count == 0) malformed("event has a zero slot count");
    const auto next = std::upper_bound(adjustments.begin(), adjustments.end(), event.measure,
                                       [](std::uint32_t measure, const PrefixAdjustment& adjustment) {
                                           return measure < adjustment.measure;
                                       });
    const double adjustment = next == adjustments.begin() ? 0.0 : std::prev(next)->total;
    const double position = static_cast<double>(event.measure) +
                            static_cast<double>(event.slot_index) / static_cast<double>(event.slot_count) - adjustment;
    if (!std::isfinite(position) || position < 0.0) malformed("event position is invalid after measure-fraction adjustment");
    return position;
}

OjnHeader parse_header(io::ByteReader& input) {
    OjnHeader header;
    header.song_id = input.u32le("song id");
    if (input.u32le("signature") != kOjnSignature) {
        throw Error(ExitCode::Runtime,
                    "Unsupported OJN format: expected an ordinary legacy OJN; Korea-era 'new' wrappers are not supported by RenderOJN 1.0.0.");
    }
    static_cast<void>(input.f32le("encryption version"));
    header.genre = input.u32le("genre");
    header.tempo = input.f32le("tempo");
    if (!std::isfinite(header.tempo) || header.tempo <= 0.0F) malformed("tempo must be finite and positive");

    static_cast<void>(input.u16le("easy level"));
    static_cast<void>(input.u16le("normal level"));
    static_cast<void>(input.u16le("hard level"));
    static_cast<void>(input.u16le("header padding"));
    for (auto& count : header.event_counts) {
        count = input.u32le("event count");
        if (count > kMaxEvents) malformed("event count exceeds limit");
    }
    for (auto& count : header.note_counts) {
        count = input.u32le("note count");
        if (count > kMaxEvents) malformed("note count exceeds limit");
    }
    for (auto& count : header.measure_counts) count = input.u32le("measure count");
    for (auto& count : header.package_counts) {
        count = input.u32le("package count");
        if (count > kMaxEvents) malformed("package count exceeds limit");
    }
    static_cast<void>(input.u16le("old encryption version"));
    static_cast<void>(input.u16le("old song id"));
    static_cast<void>(input.latin1_fixed(20, "old genre"));
    static_cast<void>(input.u32le("old cover size"));
    static_cast<void>(input.f32le("chart version"));
    header.title = input.latin1_fixed(64, "title");
    header.artist = input.latin1_fixed(32, "artist");
    header.charter = input.latin1_fixed(32, "charter");
    header.package_name = input.latin1_fixed(32, "sample package name");
    static_cast<void>(input.u32le("new cover size"));
    for (auto& duration : header.duration_seconds) {
        duration = input.u32le("duration");
        if (duration > kMaxDurationSeconds) malformed("duration exceeds six-hour limit");
    }
    for (auto& offset : header.chart_offsets) offset = input.u32le("chart offset");
    if (input.position() != kHeaderSize) malformed("unexpected header layout");
    return header;
}

} // namespace

std::shared_ptr<const io::ByteBuffer> normalize_ojn(std::shared_ptr<const io::ByteBuffer> source) {
    if (!source || source->size() < 4) malformed("truncated header");
    const auto* bytes = source->data();
    if ((bytes[0] == 'n' || bytes[0] == 'N') && (bytes[1] == 'e' || bytes[1] == 'E') &&
        (bytes[2] == 'w' || bytes[2] == 'W')) {
        throw Error(ExitCode::Runtime,
                    "Unsupported OJN format: Korea-era 'new' wrappers are not supported by RenderOJN 1.0.0; "
                    "use a legacy ordinary OJN or wait for 1.0.1 compatibility support.");
    }
    return source;
}

OjnHeader parse_ojn_header(const std::shared_ptr<const io::ByteBuffer>& buffer) {
    auto normalized = normalize_ojn(buffer);
    if (normalized->size() < kHeaderSize) malformed("truncated 300-byte header");
    io::ByteReader input(normalized);
    auto header = parse_header(input);
    if (header.chart_offsets[0] < kHeaderSize) malformed("first chart starts inside header");
    for (std::size_t index = 0; index < header.chart_offsets.size(); ++index) {
        if (header.chart_offsets[index] > normalized->size()) malformed("chart offset is outside file");
        if (index != 0 && header.chart_offsets[index] < header.chart_offsets[index - 1]) {
            malformed("chart offsets are non-monotonic or overlapping");
        }
    }
    return header;
}

Chart parse_ojn_chart(const std::shared_ptr<const io::ByteBuffer>& buffer, Difficulty difficulty) {
    auto normalized = normalize_ojn(buffer);
    const auto header = parse_ojn_header(normalized);
    const auto index = static_cast<std::size_t>(difficulty);
    const auto start = static_cast<std::size_t>(header.chart_offsets[index]);
    const auto finish = static_cast<std::size_t>(header.chart_offsets[index + 1]);
    io::ByteReader input(normalized, start, finish);

    std::size_t minimum_package_bytes{};
    if (!io::checked_multiply(static_cast<std::size_t>(header.package_counts[index]), 8U, minimum_package_bytes) ||
        minimum_package_bytes > input.remaining()) {
        malformed("declared package count cannot fit in chart section");
    }

    std::vector<RawEvent> raw_events;
    raw_events.reserve(std::min<std::uint32_t>(header.event_counts[index], kMaxEvents));
    std::uint64_t slot_count{};
    std::uint64_t event_count{};
    std::uint64_t note_count{};
    std::uint64_t ordinal{};

    for (std::uint32_t package_index = 0; package_index < header.package_counts[index]; ++package_index) {
        if (input.remaining() < 8) malformed("truncated event-set header");
        const auto measure = input.u32le("event-set measure");
        const auto channel = input.u16le("event-set channel");
        const auto count = input.u16le("event-set count");
        if (measure > header.measure_counts[index]) malformed("event-set measure exceeds declared measure count");
        std::size_t event_bytes{};
        if (!io::checked_multiply(count, 4, event_bytes) || event_bytes > input.remaining()) {
            malformed("truncated event-set records");
        }
        slot_count += count;
        if (slot_count > kMaxEvents) malformed("event slot count exceeds limit");

        for (std::uint32_t event_index = 0; event_index < count; ++event_index) {
            const auto first = input.u16le("event value");
            const auto second = input.u8("event volume");
            const auto third = input.u8("event type");
            const auto append_scalar = [&](RawKind kind, const char* field) {
                const std::uint32_t bits = static_cast<std::uint32_t>(first) | (static_cast<std::uint32_t>(second) << 16U) |
                                           (static_cast<std::uint32_t>(third) << 24U);
                float value{};
                std::memcpy(&value, &bits, sizeof(value));
                if (value == 0.0F) return;
                if (!std::isfinite(value) || value < 0.0F) malformed(std::string(field) + " must be finite and positive");
                if (kind == RawKind::MeasureFraction && measure == std::numeric_limits<std::uint32_t>::max()) {
                    malformed("measure-fraction adjustment measure overflows");
                }
                raw_events.push_back({kind, measure, static_cast<std::uint16_t>(event_index), count, 0, 0, value, ordinal++});
            };

            if (channel == 0) {
                append_scalar(RawKind::MeasureFraction, "measure fraction");
                continue;
            }
            if (channel == 1) {
                append_scalar(RawKind::Tempo, "tempo event");
                continue;
            }
            if (channel >= 2 && first != 0) {
                ++event_count;
                if (event_count > kMaxEvents) malformed("event count exceeds limit");
                if (channel <= 8) {
                    ++note_count;
                    if (note_count > kMaxEvents) malformed("note count exceeds limit");
                }
                raw_events.push_back({RawKind::Note, measure, static_cast<std::uint16_t>(event_index), count, first, third, 0.0F, ordinal++});
            }
        }
    }

    if (input.remaining() != 0) malformed("chart section has trailing bytes after declared packages");
    if (event_count != header.event_counts[index]) malformed("event count does not match header");
    if (note_count != header.note_counts[index]) malformed("note count does not match header");

    std::map<std::uint32_t, double> fraction_by_measure;
    for (const auto& event : raw_events) {
        if (event.kind == RawKind::MeasureFraction) fraction_by_measure[event.measure + 1U] = event.scalar_value;
    }
    std::vector<PrefixAdjustment> adjustments;
    adjustments.reserve(fraction_by_measure.size());
    double total_adjustment{};
    for (const auto& entry : fraction_by_measure) {
        total_adjustment += 1.0 - entry.second;
        if (!std::isfinite(total_adjustment)) malformed("measure-fraction adjustment overflows");
        adjustments.push_back({entry.first, total_adjustment});
    }

    std::vector<TimelineEvent> timeline;
    timeline.reserve(raw_events.size());
    for (const auto& event : raw_events) {
        if (event.kind != RawKind::MeasureFraction) timeline.push_back({event, effective_position(event, adjustments)});
    }
    std::stable_sort(timeline.begin(), timeline.end(), [](const TimelineEvent& left, const TimelineEvent& right) {
        if (left.position != right.position) return left.position < right.position;
        if (left.event.kind != right.event.kind) return left.event.kind == RawKind::Tempo;
        return left.event.ordinal < right.event.ordinal;
    });

    Chart chart{header, difficulty, {}};
    chart.notes.reserve(std::min<std::uint32_t>(header.event_counts[index], kMaxEvents));
    // The declared duration is song-list display metadata, not a bound on chart
    // content. Real charts routinely place notes past it (30 of 229 in the
    // O2Jam/O2Jam Thai corpus, up to +3.97s in o2ma105.ojn), and neither
    // Open2Jam nor CXO2 validates events against it. Only the absolute
    // six-hour cap bounds event time; output length follows content instead
    // (see render::output_frame_count).
    const auto absolute_frame_limit = static_cast<std::uint64_t>(kMaxDurationSeconds) * kSampleRate;
    const auto absolute_end_seconds = static_cast<double>(kMaxDurationSeconds);
    double previous_position{};
    double seconds{};
    double tempo = header.tempo;
    for (const auto& timeline_event : timeline) {
        const auto elapsed_measures = timeline_event.position - previous_position;
        if (!std::isfinite(elapsed_measures) || elapsed_measures < 0.0) malformed("event positions are not chronological");
        seconds += elapsed_measures * 4.0 * 60.0 / tempo;
        if (!std::isfinite(seconds) || seconds < 0.0) malformed("event time is invalid");
        previous_position = timeline_event.position;
        if (timeline_event.event.kind == RawKind::Tempo) {
            tempo = timeline_event.event.scalar_value;
            continue;
        }
        if (seconds >= absolute_end_seconds) malformed("event occurs beyond the six-hour output limit");
        const auto frame = seconds_to_frame(seconds);
        if (frame >= absolute_frame_limit) malformed("event occurs beyond the six-hour output limit");
        chart.notes.push_back({frame, timeline_event.event.reference_id, timeline_event.event.note_type, timeline_event.event.measure,
                               timeline_event.event.slot_index, timeline_event.event.slot_count});
    }

    for (const auto& event : chart.notes) {
        if (event.frame >= absolute_frame_limit) malformed("event occurs beyond the six-hour output limit");
    }
    std::stable_sort(chart.notes.begin(), chart.notes.end(), [](const NoteEvent& left, const NoteEvent& right) {
        return left.frame < right.frame;
    });
    return chart;
}

} // namespace renderojn::format
