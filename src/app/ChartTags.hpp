#pragma once

#include "core/Diagnostic.hpp"
#include "core/format/Types.hpp"
#include "core/io/ByteReader.hpp"
#include "core/output/Encoder.hpp"

namespace renderojn::app {

// Builds the tag set for a chart from its header and (optionally) its cover art.
// The CLI and the WebAssembly build both call this so their output tags cannot
// drift -- see the note in src/wasm/Bindings.cpp.  `normalized` is the buffer
// extract_cover_art reads; pass include_cover=false to leave the picture out.
[[nodiscard]] output::Tags build_chart_tags(const format::OjnHeader& header, const io::ByteBuffer& normalized,
                                            bool include_cover, Diagnostics& diagnostics);

} // namespace renderojn::app
