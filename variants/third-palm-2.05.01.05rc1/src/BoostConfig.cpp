#include "BoostConfig.h"

#include <Windows.h>
#include <array>
#include <charconv>
#include <string_view>
#include <utility>

namespace superjump {
namespace {

constexpr DWORD maxConfigBytes = 4096;
constexpr std::uint64_t pollIntervalMs = 1000;

std::string_view Trim(std::string_view text) noexcept {
    constexpr auto whitespace = " \t\r";
    const auto first = text.find_first_not_of(whitespace);
    if (first == text.npos) return {};
    return text.substr(first, text.find_last_not_of(whitespace) - first + 1);
}

bool EqualAscii(std::string_view text, std::string_view lowercase) noexcept {
    if (text.size() != lowercase.size()) return false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        auto c = text[i];
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
        if (c != lowercase[i]) return false;
    }
    return true;
}

std::optional<float> ReadBoostY(std::string_view text) noexcept {
    bool inSection = false;
    std::optional<float> boost;
    while (!text.empty()) {
        const auto newline = text.find('\n');
        auto line = text.substr(0, newline);
        text = newline == text.npos ? std::string_view{} : text.substr(newline + 1);
        line = Trim(line.substr(0, line.find_first_of(";#")));
        if (line.empty()) continue;
        if (line.front() == '[') {
            inSection = line.back() == ']' &&
                EqualAscii(Trim(line.substr(1, line.size() - 2)), "superjump");
            continue;
        }
        const auto separator = line.find('=');
        if (!inSection || separator == line.npos ||
            !EqualAscii(Trim(line.substr(0, separator)), "boosty")) continue;

        const auto number = Trim(line.substr(separator + 1));
        if (boost || number.empty()) return std::nullopt;
        float value{};
        const auto end = number.data() + number.size();
        const auto parsed = std::from_chars(number.data(), end, value);
        if (parsed.ec != std::errc{} || parsed.ptr != end ||
            !(value >= 0.1f && value <= 120.0f)) return std::nullopt;
        boost = value;
    }
    return boost;
}

struct Input {
    BoostConfigStatus status;
    float value{};
};

Input ReadConfig(const std::filesystem::path& path) noexcept {
    // Opening without write sharing gives the worker a complete read between
    // edits. An editor can still replace the file at this path.
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {BoostConfigStatus::Unavailable};
    std::array<char, maxConfigBytes + 1> bytes{};
    DWORD count{};
    const bool read = GetFileType(file) == FILE_TYPE_DISK &&
        ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr);
    CloseHandle(file);
    if (!read) return {BoostConfigStatus::Unavailable};
    if (count > maxConfigBytes) return {BoostConfigStatus::Invalid};

    std::string_view text(bytes.data(), count);
    if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);
    if (text.starts_with("\xFF\xFE")) {
        if (count % 2 != 0) return {BoostConfigStatus::Invalid};
        std::size_t length{};
        for (std::size_t i = 2; i < count; i += 2) {
            const auto low = static_cast<unsigned char>(bytes[i]);
            bytes[length++] = bytes[i + 1] == 0 && low <= 127 ? bytes[i] : '?';
        }
        text = {bytes.data(), length};
    }
    if (text.find('\0') != text.npos) return {BoostConfigStatus::Invalid};
    const auto boost = ReadBoostY(text);
    if (!boost) return {BoostConfigStatus::Invalid};
    return {BoostConfigStatus::Loaded, *boost};
}

} // namespace

void BoostConfig::SetPath(std::filesystem::path path) { path_ = std::move(path); }

std::optional<BoostConfigStatus> BoostConfig::Poll(std::uint64_t now) noexcept {
    if (now < nextPoll_) return std::nullopt;
    nextPoll_ = now + pollIntervalMs;
    const auto input = ReadConfig(path_);
    const bool changed = input.status == BoostConfigStatus::Loaded && input.value != Value();
    if (changed) value_.store(input.value, std::memory_order_relaxed);
    if (!changed && status_ == input.status) return std::nullopt;
    status_ = input.status;
    return input.status;
}

static_assert(std::atomic<float>::is_always_lock_free);

} // namespace superjump
