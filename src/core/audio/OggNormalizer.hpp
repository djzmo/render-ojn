#pragma once

#include "core/Diagnostic.hpp"
#include "core/format/Types.hpp"

#include <cstdint>
#include <vector>

namespace renderojn::audio {

struct NormalizedOggPayload {
    std::uint16_t sample_id{};
    const std::vector<std::uint8_t>* original{};
    std::vector<std::uint8_t> repaired_bytes;
    std::uint32_t repaired_page_count{};

    [[nodiscard]] const std::vector<std::uint8_t>& bytes() const noexcept {
        return repaired_bytes.empty() ? *original : repaired_bytes;
    }
    [[nodiscard]] bool repaired() const noexcept { return repaired_page_count != 0; }
};

[[nodiscard]] NormalizedOggPayload normalize_ogg_vorbis_pages(const format::EncodedSample& sample, Diagnostics& diagnostics);

} // namespace renderojn::audio
