#pragma once

#include "core/Diagnostic.hpp"
#include "core/format/Types.hpp"

#include <cstdint>
#include <functional>
#include <vector>

namespace renderojn::render {

enum class SchedulingMode { Quick, Realtime };
using PcmConsumer = std::function<void(const float* frames, std::size_t frame_count)>;

[[nodiscard]] std::uint64_t output_frame_count(const format::Chart& chart);
void mix_chart(const format::Chart& chart, const std::vector<format::DecodedSample>& samples, SchedulingMode mode,
               bool wall_clock_pacing, const PcmConsumer& consumer, Diagnostics& diagnostics);

} // namespace renderojn::render
