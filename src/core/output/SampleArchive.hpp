#pragma once

#include "core/format/Types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace renderojn::output {

// The archive name one sample is extracted under: "<id>_<stored name>.<ext>",
// or "<id>.<ext>" when the record has no name.  The id is zero-padded to four
// digits and is unique within a package, so names never collide.  The stored
// name is decoded to UTF-8 (it is usually CP949), stripped of a trailing
// extension equal to the one added, and made filesystem-safe.  The extension
// comes from the decoded bytes: .ogg, .wav, or .bin.
[[nodiscard]] std::string sample_entry_name(const format::EncodedSample& sample);

// Every sample of a parsed package as a ZIP archive with stored (uncompressed)
// entries and UTF-8 names.  The timestamp is fixed, so the same package always
// yields the same bytes -- the CLI and the web build produce identical files.
[[nodiscard]] std::vector<std::uint8_t> build_sample_zip(const format::Package& package);

} // namespace renderojn::output
