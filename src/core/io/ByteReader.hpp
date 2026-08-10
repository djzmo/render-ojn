#pragma once

#include "core/Diagnostic.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace renderojn::io {

class ByteBuffer {
public:
    explicit ByteBuffer(std::vector<std::uint8_t> bytes) : bytes_(std::move(bytes)) {}
    [[nodiscard]] std::size_t size() const noexcept { return bytes_.size(); }
    [[nodiscard]] const std::uint8_t* data() const noexcept { return bytes_.data(); }
    [[nodiscard]] const std::vector<std::uint8_t>& bytes() const noexcept { return bytes_; }

private:
    const std::vector<std::uint8_t> bytes_;
};

class ByteReader {
public:
    explicit ByteReader(std::shared_ptr<const ByteBuffer> buffer, std::size_t start = 0, std::size_t end = static_cast<std::size_t>(-1));

    [[nodiscard]] std::size_t position() const noexcept { return position_; }
    [[nodiscard]] std::size_t end() const noexcept { return end_; }
    [[nodiscard]] std::size_t remaining() const noexcept { return end_ - position_; }
    [[nodiscard]] std::uint8_t u8(const char* field);
    [[nodiscard]] std::uint16_t u16le(const char* field);
    [[nodiscard]] std::uint32_t u32le(const char* field);
    [[nodiscard]] float f32le(const char* field);
    [[nodiscard]] std::vector<std::uint8_t> take(std::size_t size, const char* field);
    [[nodiscard]] std::string latin1_fixed(std::size_t size, const char* field);
    void seek(std::size_t position, const char* field);

private:
    void require(std::size_t size, const char* field) const;
    std::shared_ptr<const ByteBuffer> buffer_;
    std::size_t position_{};
    std::size_t end_{};
};

[[nodiscard]] std::shared_ptr<const ByteBuffer> read_file(const std::string& path, std::uint64_t maximum_bytes, const char* kind);
[[nodiscard]] bool checked_add(std::size_t left, std::size_t right, std::size_t& result) noexcept;
[[nodiscard]] bool checked_multiply(std::size_t left, std::size_t right, std::size_t& result) noexcept;

} // namespace renderojn::io
