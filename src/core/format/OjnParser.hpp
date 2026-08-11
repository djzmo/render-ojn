#pragma once

#include "core/Diagnostic.hpp"
#include "core/format/Types.hpp"
#include "core/io/ByteReader.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace renderojn::format {

// Returns a buffer the rest of the parser can read directly: an ordinary OJN is
// passed through untouched, while a Korea-era `new` wrapper is decrypted first.
// Keeping that in one seam is what lets everything downstream stay unaware of
// the container. Callers that already hold a normalized buffer may pass it
// again; the second call is a no-op.
[[nodiscard]] std::shared_ptr<const io::ByteBuffer> normalize_ojn(std::shared_ptr<const io::ByteBuffer> source);
[[nodiscard]] OjnHeader parse_ojn_header(const std::shared_ptr<const io::ByteBuffer>& buffer);
[[nodiscard]] Chart parse_ojn_chart(const std::shared_ptr<const io::ByteBuffer>& buffer, Difficulty difficulty);

// Resolves OjnHeader::genre to its display name.  The genre is a header field,
// so its interpretation belongs beside the parser rather than in each front
// end -- the CLI and the WebAssembly binding both tag output with it, and a
// second copy of the table is a second thing to keep correct.
// An out-of-range code warns and resolves to "Etc".
[[nodiscard]] std::string genre_name(std::uint32_t genre, Diagnostics& diagnostics);

} // namespace renderojn::format
