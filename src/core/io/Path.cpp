#include "core/io/Path.hpp"

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <algorithm>
#endif

namespace renderojn::io {

std::string filesystem_collision_key(const std::filesystem::path& path) {
#ifdef _WIN32
    // NTFS is case-insensitive.  LCMAP_UPPERCASE upcases with the OS table --
    // the same mapping NTFS uses to decide two names are one file -- so the key
    // matches the filesystem rather than any private Unicode fold.
    const std::wstring& native = path.native();
    if (native.empty()) return {};
    const int length = ::LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, native.c_str(),
                                       static_cast<int>(native.size()), nullptr, 0, nullptr, nullptr, 0);
    std::wstring folded(native);
    if (length > 0) {
        folded.resize(static_cast<std::size_t>(length));
        ::LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, native.c_str(), static_cast<int>(native.size()),
                        folded.data(), length, nullptr, nullptr, 0);
    }
    return path_to_utf8(std::filesystem::path(folded));
#elif defined(__APPLE__)
    // APFS/HFS+ are case-insensitive-preserving.  A full case fold would need
    // ICU; ASCII folding covers the overwhelming majority of chart titles and
    // never merges two names the filesystem would keep apart in the ASCII range.
    // Non-ASCII case pairs on macOS are the documented gap.
    auto key = path_to_utf8(path);
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return key;
#else
    // ext4 and friends are case-sensitive: two names that differ only by case
    // are genuinely different files, so the key must stay byte-exact.
    return path_to_utf8(path);
#endif
}

} // namespace renderojn::io
