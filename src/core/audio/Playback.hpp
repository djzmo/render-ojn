#pragma once

#include "core/Diagnostic.hpp"
#include "core/format/Types.hpp"

#include <vector>

namespace renderojn::audio {

void play_realtime(const format::Chart& chart, const std::vector<format::DecodedSample>& samples, Diagnostics& diagnostics);

} // namespace renderojn::audio
