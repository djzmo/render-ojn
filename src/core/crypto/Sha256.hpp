#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace renderojn::crypto {

using Sha256Digest = std::array<std::uint8_t, 32>;

[[nodiscard]] Sha256Digest sha256(const std::vector<std::uint8_t>& bytes);
[[nodiscard]] std::string sha256_hex(const Sha256Digest& digest);

} // namespace renderojn::crypto
