#include "core/crypto/Sha256.hpp"

#include <array>
#include <limits>
#include <stdexcept>

namespace renderojn::crypto {
namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants{{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
}};

[[nodiscard]] constexpr std::uint32_t rotate_right(std::uint32_t value, unsigned int amount) noexcept {
    return (value >> amount) | (value << (32U - amount));
}

void compress(std::array<std::uint32_t, 8>& state, const std::uint8_t* block) {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t index = 0; index < 16; ++index) {
        const auto offset = index * 4U;
        words[index] = (static_cast<std::uint32_t>(block[offset]) << 24U) |
                       (static_cast<std::uint32_t>(block[offset + 1U]) << 16U) |
                       (static_cast<std::uint32_t>(block[offset + 2U]) << 8U) |
                       static_cast<std::uint32_t>(block[offset + 3U]);
    }
    for (std::size_t index = 16; index < words.size(); ++index) {
        const auto small_sigma0 = rotate_right(words[index - 15U], 7U) ^ rotate_right(words[index - 15U], 18U) ^ (words[index - 15U] >> 3U);
        const auto small_sigma1 = rotate_right(words[index - 2U], 17U) ^ rotate_right(words[index - 2U], 19U) ^ (words[index - 2U] >> 10U);
        words[index] = words[index - 16U] + small_sigma0 + words[index - 7U] + small_sigma1;
    }

    auto a = state[0]; auto b = state[1]; auto c = state[2]; auto d = state[3];
    auto e = state[4]; auto f = state[5]; auto g = state[6]; auto h = state[7];
    for (std::size_t index = 0; index < words.size(); ++index) {
        const auto sigma1 = rotate_right(e, 6U) ^ rotate_right(e, 11U) ^ rotate_right(e, 25U);
        const auto choose = (e & f) ^ (~e & g);
        const auto temporary1 = h + sigma1 + choose + kRoundConstants[index] + words[index];
        const auto sigma0 = rotate_right(a, 2U) ^ rotate_right(a, 13U) ^ rotate_right(a, 22U);
        const auto majority = (a & b) ^ (a & c) ^ (b & c);
        const auto temporary2 = sigma0 + majority;
        h = g; g = f; f = e; e = d + temporary1;
        d = c; c = b; b = a; a = temporary1 + temporary2;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

} // namespace

Sha256Digest sha256(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() > std::numeric_limits<std::uint64_t>::max() / 8U) {
        throw std::overflow_error("SHA-256 input exceeds the 64-bit length field");
    }

    std::array<std::uint32_t, 8> state{{
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U,
    }};
    std::size_t offset{};
    while (bytes.size() - offset >= 64U) {
        compress(state, bytes.data() + offset);
        offset += 64U;
    }

    std::array<std::uint8_t, 64> final_block{};
    const auto remaining = bytes.size() - offset;
    for (std::size_t index = 0; index < remaining; ++index) final_block[index] = bytes[offset + index];
    final_block[remaining] = 0x80U;
    if (remaining >= 56U) {
        compress(state, final_block.data());
        final_block.fill(0);
    }
    const auto bit_length = static_cast<std::uint64_t>(bytes.size()) * 8U;
    for (std::size_t index = 0; index < 8; ++index) {
        final_block[63U - index] = static_cast<std::uint8_t>(bit_length >> (index * 8U));
    }
    compress(state, final_block.data());

    Sha256Digest digest{};
    for (std::size_t index = 0; index < state.size(); ++index) {
        digest[index * 4U] = static_cast<std::uint8_t>(state[index] >> 24U);
        digest[index * 4U + 1U] = static_cast<std::uint8_t>(state[index] >> 16U);
        digest[index * 4U + 2U] = static_cast<std::uint8_t>(state[index] >> 8U);
        digest[index * 4U + 3U] = static_cast<std::uint8_t>(state[index]);
    }
    return digest;
}

std::string sha256_hex(const Sha256Digest& digest) {
    constexpr char kHex[] = "0123456789abcdef";
    std::string text;
    text.reserve(digest.size() * 2U);
    for (const auto byte : digest) {
        text.push_back(kHex[byte >> 4U]);
        text.push_back(kHex[byte & 0x0fU]);
    }
    return text;
}

} // namespace renderojn::crypto
