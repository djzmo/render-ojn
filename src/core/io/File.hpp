#pragma once

#include <filesystem>
#include <fstream>

namespace renderojn::io {

class TransactionalFile {
public:
    explicit TransactionalFile(std::filesystem::path destination);
    ~TransactionalFile();
    TransactionalFile(const TransactionalFile&) = delete;
    TransactionalFile& operator=(const TransactionalFile&) = delete;

    [[nodiscard]] std::ofstream& stream() noexcept { return stream_; }
    [[nodiscard]] const std::filesystem::path& temporary_path() const noexcept { return temporary_; }
    void close();
    void commit();

private:
    std::filesystem::path destination_;
    std::filesystem::path temporary_;
    std::ofstream stream_;
    bool committed_{};
};

} // namespace renderojn::io
