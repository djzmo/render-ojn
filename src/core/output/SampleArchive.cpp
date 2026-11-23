#include "core/output/SampleArchive.hpp"

#include "core/Diagnostic.hpp"
#include "core/text/Cp949.hpp"

#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace renderojn::output {
namespace {

constexpr std::uint64_t kMaxU16 = 0xFFFFU;
constexpr std::uint64_t kMaxU32 = 0xFFFFFFFFU;

// 1980-01-01 00:00, the DOS epoch.  Fixed rather than "now" so the archive is
// reproducible.
constexpr std::uint16_t kDosTime = 0;
constexpr std::uint16_t kDosDate = (1U << 5U) | 1U;

const char* extension_for(const std::vector<std::uint8_t>& bytes) {
    const auto has = [&bytes](std::size_t offset, const char* magic) {
        const auto length = std::strlen(magic);
        return bytes.size() >= offset + length && std::memcmp(bytes.data() + offset, magic, length) == 0;
    };
    if (has(0, "OggS")) return ".ogg";
    if (has(0, "RIFF") && has(8, "WAVE")) return ".wav";
    return ".bin";
}

bool ends_with_ignoring_case(const std::string& text, const std::string& suffix) {
    if (text.size() < suffix.size()) return false;
    const auto start = text.size() - suffix.size();
    for (std::size_t index = 0; index < suffix.size(); ++index) {
        if (std::tolower(static_cast<unsigned char>(text[start + index])) !=
            std::tolower(static_cast<unsigned char>(suffix[index]))) {
            return false;
        }
    }
    return true;
}

std::uint32_t crc32(const std::vector<std::uint8_t>& bytes) {
    static const auto table = [] {
        std::array<std::uint32_t, 256> result{};
        for (std::uint32_t entry = 0; entry < 256; ++entry) {
            auto value = entry;
            for (int bit = 0; bit < 8; ++bit) value = (value & 1U) != 0 ? 0xEDB88320U ^ (value >> 1U) : value >> 1U;
            result[entry] = value;
        }
        return result;
    }();
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const auto byte : bytes) crc = table[(crc ^ byte) & 0xFFU] ^ (crc >> 8U);
    return crc ^ 0xFFFFFFFFU;
}

// Appends `value` little-endian in `width` bytes.
void put(std::vector<std::uint8_t>& out, std::uint64_t value, int width) {
    for (int index = 0; index < width; ++index) out.push_back(static_cast<std::uint8_t>(value >> (8 * index)));
}

[[noreturn]] void too_large(const std::string& what) {
    throw Error(ExitCode::Runtime, "Sample package is too large to archive: " + what);
}

} // namespace

std::string sample_entry_name(const format::EncodedSample& sample) {
    const std::string extension = extension_for(sample.bytes);
    auto name = text::decode_ojn_text(sample.name);
    for (auto& character : name) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte < 0x20 || byte == 0x7F || std::strchr("\\/:*?\"<>|", character) != nullptr) character = '_';
    }
    // Windows drops trailing spaces and dots; leading spaces only confuse.
    const auto trim = [&name] {
        name.resize(name.find_last_not_of(" .") + 1);  // npos + 1 == 0: all spaces/dots
        name.erase(0, name.find_first_not_of(' '));
    };
    trim();
    // Some packages store the file name with its extension ("003_bach live -
    // master.ogg"); do not double it.  After the trim, so "x.ogg " counts too.
    if (ends_with_ignoring_case(name, extension)) {
        name.resize(name.size() - extension.size());
        trim();
    }

    std::array<char, 8> id{};
    std::snprintf(id.data(), id.size(), "%04u", static_cast<unsigned>(sample.id));
    std::string entry = id.data();
    if (!name.empty()) entry += "_" + name;
    return entry + extension;
}

std::vector<std::uint8_t> build_sample_zip(const format::Package& package) {
    // ponytail: classic ZIP only.  Real packages hold a few hundred samples and
    // tens of megabytes; add ZIP64 if one ever passes 65535 entries or 4 GiB.
    if (package.samples.size() > kMaxU16) too_large("more than 65535 samples");

    std::size_t total{};
    for (const auto& sample : package.samples) total += sample.bytes.size() + 128U;
    std::vector<std::uint8_t> out;
    out.reserve(total);
    std::vector<std::uint8_t> central;

    for (const auto& sample : package.samples) {
        const auto name = sample_entry_name(sample);
        const auto crc = crc32(sample.bytes);
        const std::uint64_t size = sample.bytes.size();
        const std::uint64_t offset = out.size();
        if (offset > kMaxU32 || size > kMaxU32) too_large("more than 4 GiB");

        put(out, 0x04034B50U, 4);  // local file header
        put(out, 20, 2);           // version needed: 2.0
        put(out, 0x0800U, 2);      // flags: UTF-8 name
        put(out, 0, 2);            // method: stored
        put(out, kDosTime, 2);
        put(out, kDosDate, 2);
        put(out, crc, 4);
        put(out, size, 4);         // compressed size
        put(out, size, 4);         // uncompressed size
        put(out, name.size(), 2);
        put(out, 0, 2);            // extra length
        out.insert(out.end(), name.begin(), name.end());
        out.insert(out.end(), sample.bytes.begin(), sample.bytes.end());

        put(central, 0x02014B50U, 4);  // central directory header
        put(central, 20, 2);           // version made by
        put(central, 20, 2);           // version needed
        put(central, 0x0800U, 2);
        put(central, 0, 2);
        put(central, kDosTime, 2);
        put(central, kDosDate, 2);
        put(central, crc, 4);
        put(central, size, 4);
        put(central, size, 4);
        put(central, name.size(), 2);
        put(central, 0, 2);            // extra length
        put(central, 0, 2);            // comment length
        put(central, 0, 2);            // disk number
        put(central, 0, 2);            // internal attributes
        put(central, 0, 4);            // external attributes
        put(central, offset, 4);
        central.insert(central.end(), name.begin(), name.end());
    }

    const std::uint64_t central_offset = out.size();
    if (central_offset + central.size() > kMaxU32) too_large("more than 4 GiB");
    out.insert(out.end(), central.begin(), central.end());

    put(out, 0x06054B50U, 4);  // end of central directory
    put(out, 0, 2);            // this disk
    put(out, 0, 2);            // disk with the central directory
    put(out, package.samples.size(), 2);
    put(out, package.samples.size(), 2);
    put(out, central.size(), 4);
    put(out, central_offset, 4);
    put(out, 0, 2);            // comment length
    return out;
}

} // namespace renderojn::output
