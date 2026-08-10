#include "core/compat/CompatibilityProfile.hpp"

#include "core/Diagnostic.hpp"
#include "core/render/Mixer.hpp"

#include <algorithm>
#include <limits>
#include <string>

namespace renderojn::compat {
namespace {

constexpr OjnOjmCompatibilityProfiles kKnownProfiles{{{
    {{0xfcU, 0x0bU, 0x3dU, 0x84U, 0x1aU, 0x0fU, 0x8fU, 0xefU, 0x5fU, 0xd6U, 0xa5U, 0x9dU, 0xcaU, 0xfdU, 0xf1U, 0xedU,
      0xffU, 0x29U, 0x15U, 0x01U, 0x6aU, 0x27U, 0x15U, 0x97U, 0x47U, 0x56U, 0xfcU, 0x5fU, 0xffU, 0x66U, 0xdfU, 0x39U}},
    {{0xd6U, 0x0fU, 0x4bU, 0x4bU, 0xeaU, 0xc2U, 0xbfU, 0x86U, 0x38U, 0xceU, 0xaaU, 0x58U, 0x9cU, 0xa4U, 0xd0U, 0x88U,
      0x80U, 0x71U, 0x8eU, 0xe7U, 0xa7U, 0xe1U, 0x66U, 0x41U, 0x7bU, 0x38U, 0x61U, 0xd3U, 0xfaU, 0x31U, 0xdaU, 0x30U}},
    1249U, 1U, 4U, 0U, 15U, 16U, 2293U,
    "applied o2ma121 OMC background timing compatibility correction (+2293 frames)",
}}};

[[noreturn]] void profile_drift(const char* reason) {
    throw Error(ExitCode::Runtime, std::string("o2ma121 compatibility profile drift: ") + reason);
}

} // namespace

const OjnOjmCompatibilityProfiles& known_ojn_ojm_profiles() {
    return kKnownProfiles;
}

void apply_ojn_ojm_profile(format::Chart& chart, const crypto::Sha256Digest& ojn_sha256,
                           const crypto::Sha256Digest& package_sha256,
                           const OjnOjmCompatibilityProfile& profile, Diagnostics& diagnostics) {
    if (ojn_sha256 != profile.ojn_sha256 || package_sha256 != profile.package_sha256) return;
    if (chart.notes.size() != profile.expected_timeline_event_count) profile_drift("unexpected timeline event cardinality");

    auto target = chart.notes.end();
    for (auto event = chart.notes.begin(); event != chart.notes.end(); ++event) {
        const bool matches = event->reference_id == profile.expected_reference_id && event->note_type == profile.expected_note_type &&
                             event->measure == profile.expected_measure && event->slot_index == profile.expected_slot_index &&
                             event->slot_count == profile.expected_slot_count;
        if (!matches) continue;
        if (target != chart.notes.end()) profile_drift("expected background event is ambiguous");
        target = event;
    }
    if (target == chart.notes.end()) profile_drift("expected background event is absent");
    if (target->frame > std::numeric_limits<std::uint64_t>::max() - profile.frame_delay) {
        profile_drift("background frame delay overflows");
    }
    const auto corrected_frame = target->frame + profile.frame_delay;
    // Ask the renderer for the window rather than recomputing it.  The bound is
    // max(declared duration, final note) + one second, so a chart whose content
    // runs past its declared duration still has a correct limit here; an
    // independent `duration + 1s` copy would drift from render::output_frame_count.
    const auto original_frame = target->frame;
    target->frame = corrected_frame;
    if (corrected_frame >= render::output_frame_count(chart)) {
        target->frame = original_frame;
        profile_drift("background frame delay exceeds render window");
    }
    std::stable_sort(chart.notes.begin(), chart.notes.end(), [](const format::NoteEvent& left, const format::NoteEvent& right) {
        return left.frame < right.frame;
    });
    diagnostics.warn(profile.warning);
}

void apply_known_ojn_ojm_profiles(format::Chart& chart, const io::ByteBuffer& ojn,
                                  const io::ByteBuffer& package, Diagnostics& diagnostics) {
    const auto ojn_sha256 = crypto::sha256(ojn.bytes());
    const auto package_sha256 = crypto::sha256(package.bytes());
    for (const auto& profile : known_ojn_ojm_profiles()) {
        apply_ojn_ojm_profile(chart, ojn_sha256, package_sha256, profile, diagnostics);
    }
}

} // namespace renderojn::compat
