#pragma once

#include "core/format/Types.hpp"
#include "core/io/ByteReader.hpp"

#include <memory>

namespace renderojn::format {

[[nodiscard]] Package parse_sample_package(const std::shared_ptr<const io::ByteBuffer>& buffer);

} // namespace renderojn::format
