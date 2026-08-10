#pragma once

#include <cstdint>
#include <optional>

namespace renderojn::audio::detail {

// Checked signed arithmetic for libsndfile's in-memory virtual I/O seek callback.
[[nodiscard]] std::optional<std::int64_t> checked_memory_seek_position(std::int64_t base, std::int64_t offset,
                                                                         std::int64_t length) noexcept;

} // namespace renderojn::audio::detail
