#pragma once

#include "core/Diagnostic.hpp"
#include "core/format/Types.hpp"

#include <cstdint>
#include <functional>
#include <vector>

namespace renderojn::render {

enum class SchedulingMode { Quick, Realtime };

// Which note roles to sound. Timing and output length are identical across all
// three: the unselected notes are muted, not removed, so tempo changes and the
// content-derived duration are unaffected (output_frame_count reads every note
// regardless of selection). Keysounds are the playable lanes (channels 2-8);
// Background is the autoplay/BGM stream (channels 9+). See format::NoteEvent.
enum class TrackSelection : std::uint8_t { All, Keysounds, Background };

using PcmConsumer = std::function<void(const float* frames, std::size_t frame_count)>;

[[nodiscard]] std::uint64_t output_frame_count(const format::Chart& chart);
void mix_chart(const format::Chart& chart, const std::vector<format::DecodedSample>& samples, SchedulingMode mode,
               bool wall_clock_pacing, const PcmConsumer& consumer, Diagnostics& diagnostics,
               TrackSelection tracks = TrackSelection::All);

} // namespace renderojn::render
