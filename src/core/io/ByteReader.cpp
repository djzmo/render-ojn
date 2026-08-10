#include "core/io/ByteReader.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>

namespace renderojn::io {
namespace {

[[noreturn]] void malformed(const std::string& text) {
    throw Error(ExitCode::Runtime, "Malformed input: " + text);
}

} // namespace

ByteReader::ByteReader(std::shared_ptr<const ByteBuffer> buffer, std::size_t start, std::size_t end)
    : buffer_(std::move(buffer)), position_(start), end_(end) {
    // end_ must not be resolved from buffer_->size() in the initializer list: that
    // dereferences a null buffer before the guard below can reject it.
    if (!buffer_) malformed("invalid bounded reader range");
    if (end_ == static_cast<std::size_t>(-1)) end_ = buffer_->size();
    if (start > end_ || end_ > buffer_->size()) {
        malformed("invalid bounded reader range");
    }
}

void ByteReader::require(std::size_t size, const char* field) const {
    std::size_t finish{};
    if (!checked_add(position_, size, finish) || finish > end_) {
        malformed(std::string("truncated ") + field);
    }
}

std::uint8_t ByteReader::u8(const char* field) {
    require(1, field);
    return buffer_->data()[position_++];
}

std::uint16_t ByteReader::u16le(const char* field) {
    require(2, field);
    const auto* p = buffer_->data() + position_;
    position_ += 2;
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(p[0]) |
                                      static_cast<std::uint16_t>(static_cast<std::uint16_t>(p[1]) << 8U));
}

std::uint32_t ByteReader::u32le(const char* field) {
    require(4, field);
    const auto* p = buffer_->data() + position_;
    position_ += 4;
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8U) |
           (static_cast<std::uint32_t>(p[2]) << 16U) | (static_cast<std::uint32_t>(p[3]) << 24U);
}

float ByteReader::f32le(const char* field) {
    const std::uint32_t bits = u32le(field);
    float value{};
    static_assert(sizeof(value) == sizeof(bits));
    std::memcpy(&value, &bits, sizeof(value));
    if (!std::isfinite(value)) {
        malformed(std::string("non-finite ") + field);
    }
    return value;
}

std::vector<std::uint8_t> ByteReader::take(std::size_t size, const char* field) {
    require(size, field);
    const auto begin = buffer_->bytes().begin() + static_cast<std::ptrdiff_t>(position_);
    position_ += size;
    return {begin, begin + static_cast<std::ptrdiff_t>(size)};
}

std::string ByteReader::latin1_fixed(std::size_t size, const char* field) {
    auto value = take(size, field);
    const auto terminator = std::find(value.begin(), value.end(), std::uint8_t{0});
    std::string result;
    result.reserve(static_cast<std::size_t>(std::distance(value.begin(), terminator)));
    for (auto iterator = value.begin(); iterator != terminator; ++iterator) {
        result.push_back(static_cast<char>(*iterator));
    }
    return result;
}

void ByteReader::seek(std::size_t position, const char* field) {
    if (position > end_) {
        malformed(std::string("out-of-range ") + field);
    }
    position_ = position;
}

std::shared_ptr<const ByteBuffer> read_file(const std::string& path, std::uint64_t maximum_bytes, const char* kind) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) {
        throw Error(ExitCode::Runtime, std::string("Unable to read ") + kind + ": " + path);
    }
    if (size > maximum_bytes || size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw Error(ExitCode::Runtime, std::string(kind) + " exceeds the 2 GiB input limit");
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw Error(ExitCode::Runtime, std::string("Unable to open ") + kind + ": " + path);
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty() && !file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        throw Error(ExitCode::Runtime, std::string("Unable to read ") + kind + ": " + path);
    }
    return std::make_shared<ByteBuffer>(std::move(bytes));
}

bool checked_add(std::size_t left, std::size_t right, std::size_t& result) noexcept {
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        return false;
    }
    result = left + right;
    return true;
}

bool checked_multiply(std::size_t left, std::size_t right, std::size_t& result) noexcept {
    if (left != 0 && right > std::numeric_limits<std::size_t>::max() / left) {
        return false;
    }
    result = left * right;
    return true;
}

} // namespace renderojn::io
