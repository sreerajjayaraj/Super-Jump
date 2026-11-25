#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <optional>

namespace superjump {

inline constexpr float defaultBoostY = 40.0f;

enum class BoostConfigStatus { Loaded, Unavailable, Invalid };

class BoostConfig {
public:
    void SetPath(std::filesystem::path path);
    std::optional<BoostConfigStatus> Poll(std::uint64_t now) noexcept;
    float Value() const noexcept { return value_.load(std::memory_order_relaxed); }

private:
    std::atomic<float> value_{defaultBoostY};
    std::filesystem::path path_;
    std::optional<BoostConfigStatus> status_;
    std::uint64_t nextPoll_{};
};

} // namespace superjump
