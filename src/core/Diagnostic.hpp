#pragma once

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace renderojn {

enum class ExitCode : int { Success = 0, Runtime = 1, Usage = 2 };

class Error final : public std::runtime_error {
public:
    Error(ExitCode code, std::string message) : std::runtime_error(std::move(message)), code_(code) {}
    [[nodiscard]] ExitCode code() const noexcept { return code_; }

private:
    ExitCode code_;
};

class Diagnostics {
public:
    void warn(std::string text) {
        constexpr std::size_t maximum_warnings = 1000;
        if (warnings_.size() < maximum_warnings) {
            warnings_.push_back(std::move(text));
        } else if (!truncated_) {
            warnings_.push_back("additional warnings suppressed");
            truncated_ = true;
        }
    }
    [[nodiscard]] const std::vector<std::string>& warnings() const noexcept { return warnings_; }

private:
    std::vector<std::string> warnings_;
    bool truncated_{};
};

} // namespace renderojn
