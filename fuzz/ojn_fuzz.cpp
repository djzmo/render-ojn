#include "core/format/OjnParser.hpp"
#include "core/io/ByteReader.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    try {
        std::vector<std::uint8_t> copy(data, data + size);
        auto input = std::make_shared<renderojn::io::ByteBuffer>(std::move(copy));
        static_cast<void>(renderojn::format::parse_ojn_header(input));
        static_cast<void>(renderojn::format::parse_ojn_chart(input, renderojn::format::Difficulty::Hard));
    } catch (...) {}
    return 0;
}
