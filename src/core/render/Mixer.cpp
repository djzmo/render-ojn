#include "core/render/Mixer.hpp"

#include "core/Diagnostic.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <limits>
#include <string>
#include <thread>
#include <unordered_map>

namespace renderojn::render {
namespace {

constexpr std::size_t kQuickBlockFrames = 1024;
constexpr std::size_t kRealtimeBlockFrames = 48;
constexpr std::size_t kVoiceLimit = 100;

struct Voice {
    const format::DecodedSample* sample{};
    std::size_t cursor{};
};

bool note_selected(const format::NoteEvent& event, TrackSelection tracks) {
    switch (tracks) {
        case TrackSelection::Keysounds: return event.is_keysound;
        case TrackSelection::Background: return !event.is_keysound;
        case TrackSelection::All: break;
    }
    return true;
}

void add_voice(const format::NoteEvent& event, const std::unordered_map<std::uint16_t, const format::DecodedSample*>& samples,
               std::vector<Voice>& voices, Diagnostics& diagnostics, TrackSelection tracks) {
    // Muting an unselected note here, rather than filtering chart.notes upstream,
    // is what keeps every mode the same length: the scheduling loop still walks
    // the whole event list and output_frame_count still sees the final onset.
    if (!note_selected(event, tracks)) return;
    if (event.note_type == 3) return;
    const auto requested = static_cast<std::uint32_t>(event.reference_id) + (event.note_type == 4 ? 1000U : 0U);
    if (requested > std::numeric_limits<std::uint16_t>::max()) {
        diagnostics.warn("sample reference overflows for note " + std::to_string(event.reference_id));
        return;
    }
    // A reference to a slot the package does not populate is normal, not a
    // defect: 778 of the 958 charts in the validation corpus do it, 35,145
    // times in total.  The note is skipped and mixing continues.  This used to
    // warn, but a diagnostic that fires on four files in five is noise, and it
    // devalued the warnings that do mean something -- compatibility
    // corrections and empty directory records.
    const auto iterator = samples.find(static_cast<std::uint16_t>(requested));
    if (iterator == samples.end()) return;
    if (voices.size() >= kVoiceLimit) {
        diagnostics.warn("voice limit reached; dropping newest trigger");
        return;
    }
    voices.push_back({iterator->second, 0});
}

void mix_span(std::vector<Voice>& voices, std::vector<float>& output, std::size_t output_offset, std::size_t frame_count) {
    auto write = voices.begin();
    for (auto voice = voices.begin(); voice != voices.end(); ++voice) {
        const auto available = voice->sample->stereo_frames.size() / 2U - voice->cursor;
        const auto mixed = std::min(available, frame_count);
        for (std::size_t frame = 0; frame < mixed; ++frame) {
            const auto output_frame = output_offset + frame;
            output[output_frame * 2U] += voice->sample->stereo_frames[(voice->cursor + frame) * 2U];
            output[output_frame * 2U + 1U] += voice->sample->stereo_frames[(voice->cursor + frame) * 2U + 1U];
        }
        voice->cursor += mixed;
        if (voice->cursor < voice->sample->stereo_frames.size() / 2U) *write++ = *voice;
    }
    voices.erase(write, voices.end());
}

} // namespace

std::uint64_t output_frame_count(const format::Chart& chart) {
    // The declared duration is display metadata and can under-report the real
    // chart length; 30 of the 229 charts in the O2Jam/O2Jam Thai corpus place
    // notes past it. Take whichever of declaration and content runs longer so
    // late notes are never clipped, then add the terminal second that has
    // always covered sample decay past the final onset.
    const auto seconds = chart.header.duration_seconds[static_cast<std::size_t>(chart.difficulty)];
    const auto declared_frames = static_cast<std::uint64_t>(seconds) * format::kSampleRate;
    // parse_ojn_chart leaves notes sorted by frame, and every later mutation
    // (compatibility profiles) re-sorts, so the final entry carries the maximum.
    // Assert rather than rescan: at the one-million-event cap a linear scan here
    // runs twice per render, once from the CLI and once from mix_chart.
    assert(std::is_sorted(chart.notes.begin(), chart.notes.end(),
                          [](const format::NoteEvent& left, const format::NoteEvent& right) {
                              return left.frame < right.frame;
                          }));
    const std::uint64_t content_frames = chart.notes.empty() ? 0U : chart.notes.back().frame;
    const auto tail = static_cast<std::uint64_t>(format::kSampleRate);
    const auto limit = static_cast<std::uint64_t>(format::kMaxDurationSeconds) * format::kSampleRate;
    return std::min(std::max(declared_frames, content_frames) + tail, limit);
}

void mix_chart(const format::Chart& chart, const std::vector<format::DecodedSample>& samples, SchedulingMode mode,
               bool wall_clock_pacing, const PcmConsumer& consumer, Diagnostics& diagnostics, TrackSelection tracks) {
    // A track selection that matches no note in this chart is not an error --
    // some charts carry no background events at all, and every playable-only
    // chart has an empty background stream -- but it renders silence, so say so
    // rather than hand back a valid-looking but empty file.
    if (tracks != TrackSelection::All &&
        std::none_of(chart.notes.begin(), chart.notes.end(),
                     [tracks](const format::NoteEvent& note) { return note_selected(note, tracks); })) {
        diagnostics.warn(tracks == TrackSelection::Keysounds ? "chart has no keysound notes; output will be silent"
                                                             : "chart has no background notes; output will be silent");
    }
    std::unordered_map<std::uint16_t, const format::DecodedSample*> sample_index;
    for (const auto& sample : samples) {
        if (sample.stereo_frames.size() % 2U != 0) throw Error(ExitCode::Runtime, "Internal sample has a non-stereo frame count");
        sample_index.emplace(sample.id, &sample);
    }
    const auto total_frames = output_frame_count(chart);
    const std::size_t nominal_block = mode == SchedulingMode::Quick ? kQuickBlockFrames : kRealtimeBlockFrames;
    std::vector<Voice> voices;
    std::vector<float> block;
    std::size_t event_index{};
    std::uint64_t frame{};
    const auto started = std::chrono::steady_clock::now();

    while (frame < total_frames) {
        const auto frames = static_cast<std::size_t>(std::min<std::uint64_t>(nominal_block, total_frames - frame));
        if (wall_clock_pacing) {
            const auto deadline = started + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                               std::chrono::duration<double>(static_cast<double>(frame) / format::kSampleRate));
            const auto now = std::chrono::steady_clock::now();
            if (deadline > now) std::this_thread::sleep_until(deadline);
        }
        if (mode == SchedulingMode::Quick) {
            block.assign(frames * 2U, 0.0F);
            const auto block_end = frame + frames;
            while (event_index < chart.notes.size() && chart.notes[event_index].frame <= frame) {
                add_voice(chart.notes[event_index++], sample_index, voices, diagnostics, tracks);
            }

            std::size_t span_start{};
            while (event_index < chart.notes.size() && chart.notes[event_index].frame < block_end) {
                const auto target_frame = chart.notes[event_index].frame;
                const auto target_offset = static_cast<std::size_t>(target_frame - frame);
                // Keep one complete output block for the consumer while advancing
                // active voices only through the interval before this trigger.
                mix_span(voices, block, span_start, target_offset - span_start);
                span_start = target_offset;
                do {
                    add_voice(chart.notes[event_index++], sample_index, voices, diagnostics, tracks);
                } while (event_index < chart.notes.size() && chart.notes[event_index].frame == target_frame);
            }
            mix_span(voices, block, span_start, frames - span_start);
        } else {
            // Realtime deliberately quantizes onsets to the 48-frame block start
            // rather than placing them at exact intra-block offsets the way quick
            // mode does; see docs/compatibility.md and the "realtime scheduler
            // retains 48-frame onset quantization" test, which pins this.
            while (event_index < chart.notes.size() && chart.notes[event_index].frame <= frame) {
                add_voice(chart.notes[event_index++], sample_index, voices, diagnostics, tracks);
            }
            block.assign(frames * 2U, 0.0F);
            mix_span(voices, block, 0, frames);
        }
        consumer(block.data(), frames);
        frame += frames;
    }
}

} // namespace renderojn::render
