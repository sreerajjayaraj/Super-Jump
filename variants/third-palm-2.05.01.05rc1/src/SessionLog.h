#pragma once

#include <Windows.h>
#include <filesystem>
#include <memory>
#include <string_view>

namespace superjump {

enum class LogLevel { Info, Warn, Error };

class SessionLog {
public:
    SessionLog();
    ~SessionLog();
    SessionLog(const SessionLog&) = delete;
    SessionLog& operator=(const SessionLog&) = delete;
    void Start(const std::filesystem::path& module, const std::filesystem::path& game) noexcept;
    void Emit(LogLevel level, std::string_view component, std::string_view event,
              std::string_view fields = {}) noexcept;
    void Close() noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace superjump
