#pragma once

#include "core/format/Types.hpp"
#include "core/io/ByteReader.hpp"

#include <memory>

namespace renderojn::format {

// Returns a buffer the rest of the parser can read directly: an ordinary OJN is
// passed through untouched, while a Korea-era `new` wrapper is decrypted first.
// Keeping that in one seam is what lets everything downstream stay unaware of
// the container. Callers that already hold a normalized buffer may pass it
// again; the second call is a no-op.
[[nodiscard]] std::shared_ptr<const io::ByteBuffer> normalize_ojn(std::shared_ptr<const io::ByteBuffer> source);
[[nodiscard]] OjnHeader parse_ojn_header(const std::shared_ptr<const io::ByteBuffer>& buffer);
[[nodiscard]] Chart parse_ojn_chart(const std::shared_ptr<const io::ByteBuffer>& buffer, Difficulty difficulty);

} // namespace renderojn::format
