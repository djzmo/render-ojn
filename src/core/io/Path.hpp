#pragma once

#include <filesystem>
#include <string>
#include <string_view>

// UTF-8 is the one text encoding the program speaks: every std::string that
// names a file (CLI arguments, header fields, error messages) is UTF-8.  On
// Windows std::filesystem::path is wchar_t-native, and its narrow constructors
// and .string() go through the active code page, which corrupts anything
// outside the user's locale.  These two helpers are the only sanctioned way to
// cross between the two representations.
namespace renderojn::io {

[[nodiscard]] inline std::filesystem::path utf8_to_path(std::string_view utf8) {
    return std::filesystem::u8path(utf8.begin(), utf8.end());
}

[[nodiscard]] inline std::string path_to_utf8(const std::filesystem::path& path) {
    // u8string() is std::string in C++17 and std::u8string in C++20; the range
    // copy compiles unchanged under either.
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}

} // namespace renderojn::io
