#pragma once

#include "core/format/Types.hpp"

#include "core/Diagnostic.hpp"

#include <vector>

namespace renderojn::audio {

[[nodiscard]] std::vector<format::DecodedSample> decode_samples(const format::Package& package, Diagnostics& diagnostics);

} // namespace renderojn::audio
