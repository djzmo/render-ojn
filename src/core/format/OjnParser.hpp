#pragma once

#include "core/Diagnostic.hpp"
#include "core/format/Types.hpp"
#include "core/io/ByteReader.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

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

struct CoverArt {
    std::vector<std::uint8_t> bytes;
    std::string mime;  // "image/jpeg" or "image/bmp"
};

// The chart's cover, if it carries one.  The JPEG (the game's newer format)
// sits at chart_offsets[3] for new_cover_size bytes, with the older BMP right
// after it for old_cover_size bytes; the JPEG is preferred.  A cover that does
// not fit the file or does not start with its format's signature is reported
// as a warning and skipped -- a bad picture must never fail a render.  Expects
// the normalized buffer, as parse_ojn_header does.
[[nodiscard]] std::optional<CoverArt> extract_cover_art(const io::ByteBuffer& normalized, const OjnHeader& header,
                                                        Diagnostics& diagnostics);

} // namespace renderojn::format
