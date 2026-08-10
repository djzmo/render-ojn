#pragma once

#include "core/format/Types.hpp"
#include "core/io/ByteReader.hpp"

#include <memory>

namespace renderojn::format {

// This seam is deliberately isolated: 1.0.0 only passes ordinary OJN buffers.
// A future release can add a decrypting normalizer without changing the parser.
[[nodiscard]] std::shared_ptr<const io::ByteBuffer> normalize_ojn(std::shared_ptr<const io::ByteBuffer> source);
[[nodiscard]] OjnHeader parse_ojn_header(const std::shared_ptr<const io::ByteBuffer>& buffer);
[[nodiscard]] Chart parse_ojn_chart(const std::shared_ptr<const io::ByteBuffer>& buffer, Difficulty difficulty);

} // namespace renderojn::format
