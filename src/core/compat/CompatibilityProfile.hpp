#pragma once

#include "core/Diagnostic.hpp"
#include "core/crypto/Sha256.hpp"
#include "core/format/Types.hpp"
#include "core/io/ByteReader.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace renderojn::compat {

struct OjnOjmCompatibilityProfile {
    crypto::Sha256Digest ojn_sha256{};
    crypto::Sha256Digest package_sha256{};
    std::size_t expected_timeline_event_count{};
    std::uint16_t expected_reference_id{};
    std::uint8_t expected_note_type{};
    std::uint32_t expected_measure{};
    std::uint16_t expected_slot_index{};
    std::uint16_t expected_slot_count{};
    std::uint64_t frame_delay{};
    const char* warning{};
};

using OjnOjmCompatibilityProfiles = std::array<OjnOjmCompatibilityProfile, 1>;

[[nodiscard]] const OjnOjmCompatibilityProfiles& known_ojn_ojm_profiles();
void apply_ojn_ojm_profile(format::Chart& chart, const crypto::Sha256Digest& ojn_sha256,
                           const crypto::Sha256Digest& package_sha256,
                           const OjnOjmCompatibilityProfile& profile, Diagnostics& diagnostics);
void apply_known_ojn_ojm_profiles(format::Chart& chart, const io::ByteBuffer& ojn,
                                  const io::ByteBuffer& package, Diagnostics& diagnostics);

} // namespace renderojn::compat
