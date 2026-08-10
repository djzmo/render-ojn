#include "core/format/PackageParser.hpp"
#include "core/io/ByteReader.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    try {
        std::vector<std::uint8_t> copy(data, data + size);
        auto input = std::make_shared<renderojn::io::ByteBuffer>(std::move(copy));
        static_cast<void>(renderojn::format::parse_sample_package(input));
    } catch (...) {}
    return 0;
}
