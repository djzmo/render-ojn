#include "core/io/File.hpp"

#include "core/Diagnostic.hpp"
#include "core/io/Path.hpp"

#include <chrono>
#include <random>

#ifdef _WIN32
#include <windows.h>
#endif

namespace renderojn::io {
namespace {

std::filesystem::path unique_temporary_path(const std::filesystem::path& destination) {
    std::random_device device;
    std::mt19937_64 random(device());
    const auto parent = destination.parent_path().empty() ? std::filesystem::current_path() : destination.parent_path();
    for (int attempt = 0; attempt < 32; ++attempt) {
        const auto token = std::to_string(random());
        // Concatenate as paths rather than through .string(): on Windows the
        // narrow round trip goes through the active code page and mangles
        // any filename outside it.
        auto name = std::filesystem::path(".");
        name += destination.filename();
        name += ".renderojn-" + token + ".tmp";
        const auto candidate = parent / name;
        if (!std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    throw Error(ExitCode::Runtime, "Unable to allocate a transactional output path");
}

} // namespace

TransactionalFile::TransactionalFile(std::filesystem::path destination) : destination_(std::move(destination)), temporary_(unique_temporary_path(destination_)) {
    std::error_code error;
    const auto parent = temporary_.parent_path();
    if (!parent.empty() && !std::filesystem::exists(parent, error)) {
        throw Error(ExitCode::Runtime, "Output directory does not exist: " + path_to_utf8(parent));
    }
    stream_.open(temporary_, std::ios::binary | std::ios::trunc);
    if (!stream_) {
        throw Error(ExitCode::Runtime, "Unable to create temporary output in: " + path_to_utf8(parent));
    }
}

TransactionalFile::~TransactionalFile() {
    stream_.close();
    if (!committed_) {
        std::error_code ignored;
        std::filesystem::remove(temporary_, ignored);
    }
}

void TransactionalFile::commit() {
    close();
    std::error_code error;
#ifdef _WIN32
    if (!::MoveFileExW(temporary_.c_str(), destination_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        throw Error(ExitCode::Runtime, "Unable to publish output: " + path_to_utf8(destination_));
    }
#else
    std::filesystem::rename(temporary_, destination_, error);
    if (error) {
        throw Error(ExitCode::Runtime, "Unable to publish output: " + path_to_utf8(destination_));
    }
#endif
    committed_ = true;
}

void TransactionalFile::close() {
    if (!stream_.is_open()) return;
    stream_.flush();
    stream_.close();
    if (!stream_) throw Error(ExitCode::Runtime, "Unable to finish temporary output");
}

} // namespace renderojn::io
