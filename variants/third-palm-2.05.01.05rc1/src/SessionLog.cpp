#include "SessionLog.h"
#include "ModVersion.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <thread>
#include <map>
#include <vector>

namespace superjump {
namespace {
using Path = std::filesystem::path;
using Clock = std::chrono::steady_clock;
constexpr std::size_t fileLimit = 5 * 1024 * 1024;
constexpr std::size_t queueLimit = 256;
constexpr std::size_t inventoryLimit = 1600;

std::string Timestamp() {
    SYSTEMTIME utc{}, local{};
    FILETIME utcFile{}, localFile{};
    GetSystemTime(&utc);
    if (!SystemTimeToTzSpecificLocalTimeEx(nullptr, &utc, &local)) local = utc;
    SystemTimeToFileTime(&utc, &utcFile);
    SystemTimeToFileTime(&local, &localFile);
    const auto value = [](FILETIME ft) {
        return (static_cast<long long>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    };
    const auto offset = (value(localFile) - value(utcFile)) / 600000000;
    const auto minutes = offset < 0 ? -offset : offset;
    char buffer[48]{};
    std::snprintf(buffer, sizeof(buffer), "%04u-%02u-%02uT%02u:%02u:%02u.%03u%c%02lld:%02lld",
        unsigned(local.wYear), unsigned(local.wMonth), unsigned(local.wDay), unsigned(local.wHour),
        unsigned(local.wMinute), unsigned(local.wSecond), unsigned(local.wMilliseconds),
        offset < 0 ? '-' : '+', minutes / 60, minutes % 60);
    return buffer;
}

std::string GameBuild(const Path& game) {
    DWORD ignored{};
    const auto size = GetFileVersionInfoSizeW(game.c_str(), &ignored);
    if (!size) return "Unknown";
    std::vector<std::uint8_t> buffer(size);
    void* fixed{};
    UINT length{};
    if (!GetFileVersionInfoW(game.c_str(), 0, size, buffer.data()) ||
        !VerQueryValueW(buffer.data(), L"\\", &fixed, &length) ||
        length < sizeof(VS_FIXEDFILEINFO)) return "Unknown";
    const auto& version = *static_cast<const VS_FIXEDFILEINFO*>(fixed);
    if (version.dwSignature != 0xFEEF04BD) return "Unknown";
    return std::to_string(HIWORD(version.dwFileVersionMS)) + "." +
           std::to_string(LOWORD(version.dwFileVersionMS)) + "." +
           std::to_string(HIWORD(version.dwFileVersionLS)) + "." +
           std::to_string(LOWORD(version.dwFileVersionLS));
}

std::string EscapeName(const std::wstring& name) {
    const auto count = WideCharToMultiByte(CP_UTF8, 0, name.data(), static_cast<int>(name.size()), nullptr, 0, nullptr, nullptr);
    if (!count) return {};
    std::string encoded(count, '\0'), result;
    WideCharToMultiByte(CP_UTF8, 0, name.data(), static_cast<int>(name.size()), encoded.data(), count, nullptr, nullptr);
    for (const char c : encoded) {
        if (c == '"' || c == '\\' || c == ',') result.push_back('\\');
        result.push_back(static_cast<unsigned char>(c) < 32 ? '?' : c);
    }
    return result;
}

Path Segment(const Path& dir, const std::wstring& family, unsigned index) {
    return dir / (family + (index ? L".0" + std::to_wstring(index) : L"") + L".log");
}

std::wstring LeaseName(const Path& folder, const std::wstring& family) {
    const auto key = (folder / family).wstring();
    std::uint64_t hash = 14695981039346656037ULL;
    for (wchar_t c : key) {
        if (c >= L'A' && c <= L'Z') c += L'a' - L'A';
        hash = (hash ^ c) * 1099511628211ULL;
    }
    return L"Local\\SuperJumpThirdPalm.Log." + std::to_wstring(hash);
}

bool Missing(DWORD error) {
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}

void PrunePidFamilies(const Path& folder, const std::wstring& stem, const std::wstring& active) {
    const auto prefix = stem + L".";
    std::error_code ec;
    std::map<std::wstring, std::filesystem::file_time_type> families;
    std::filesystem::directory_iterator it(folder, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        const auto name = it->path().filename().wstring();
        if (!name.starts_with(prefix) || !name.ends_with(L".log")) continue;
        const auto extension = name.find(L'.', prefix.size());
        if (extension == std::wstring::npos || extension == prefix.size()) continue;
        const auto pid = std::wstring_view(name).substr(prefix.size(), extension - prefix.size());
        if (pid.size() > 10 || !std::ranges::all_of(pid, [](wchar_t c) { return c >= L'0' && c <= L'9'; }))
            continue;
        const auto tail = std::wstring_view(name).substr(extension);
        if (tail != L".log" && tail != L".01.log" && tail != L".02.log" && tail != L".03.log") continue;
        const auto attribute = GetFileAttributesW(it->path().c_str());
        if (attribute == INVALID_FILE_ATTRIBUTES ||
            (attribute & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) continue;
        const auto family = name.substr(0, extension);
        if (family == active) continue;
        std::error_code fileError;
        const auto written = it->last_write_time(fileError);
        if (fileError) continue;
        const auto [found, first] = families.try_emplace(family, written);
        if (!first && written > found->second) found->second = written;
    }
    if (ec) return;
    using Family = std::pair<std::wstring, std::filesystem::file_time_type>;
    std::vector<Family> sorted(families.begin(), families.end());
    std::ranges::sort(sorted, [](const auto& left, const auto& right) { return left.second > right.second; });
    constexpr std::size_t maxHistory = 8;
    for (std::size_t i = maxHistory; i < sorted.size(); ++i) {
        const auto& family = sorted[i].first;
        const auto lock = CreateMutexW(nullptr, FALSE, LeaseName(folder, family).c_str());
        if (!lock) continue;
        if (GetLastError() != ERROR_ALREADY_EXISTS)
            for (unsigned history = 0; history <= 3; ++history)
                DeleteFileW(Segment(folder, family, history).c_str());
        CloseHandle(lock);
    }
}

} // namespace

struct SessionLog::State {
    struct Line { LogLevel level; std::string text; };
    Path folder;
    std::wstring family;
    std::string sid;
    HANDLE lease{};
    HANDLE file{INVALID_HANDLE_VALUE};
    std::size_t bytes{};
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<Line> queue;
    std::thread writer;
    bool stop{};
    std::size_t dropped{};

    ~State() {
        if (writer.joinable()) writer.join();
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        if (lease) CloseHandle(lease);
    }

    std::string Format(LogLevel level, std::string_view component, std::string_view event,
                       std::string_view fields) const {
        const auto severity = level == LogLevel::Info ? "INFO" : level == LogLevel::Warn ? "WARN" : "ERROR";
        auto line = "[" + Timestamp() + "] [" + severity + "] [";
        line += component;
        line += "] sid=" + sid + " event=";
        line += event;
        if (!fields.empty()) { line += ' '; line += fields; }
        line += '\n';
        return line;
    }

    bool Own(const std::wstring& name) {
        const auto lock = CreateMutexW(nullptr, FALSE, LeaseName(folder, name).c_str());
        if (!lock) return false;
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            CloseHandle(lock);
            SetLastError(ERROR_ALREADY_EXISTS);
            return false;
        }
        lease = lock;
        family = name;
        return true;
    }

    bool Rotate() {
        for (unsigned index = 3; index > 0; --index) {
            const auto from = Segment(folder, family, index - 1);
            const auto to = Segment(folder, family, index);
            if (!MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING) &&
                !Missing(GetLastError())) return false;
        }
        const auto active = Segment(folder, family, 0);
        file = CreateFileW(active.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
        bytes = 0;
        return file != INVALID_HANDLE_VALUE;
    }

    bool Write(const std::string& line) {
        if (line.size() > fileLimit) return false;
        if (bytes + line.size() > fileLimit) {
            if (!FlushFileBuffers(file)) return false;
            CloseHandle(file);
            file = INVALID_HANDLE_VALUE;
            if (!Rotate()) return false;
        }
        DWORD written{};
        if (!WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written, nullptr) ||
            written != line.size()) return false;
        bytes += written;
        return true;
    }

    void Run() noexcept {
        try {
            auto flushAt = Clock::now() + std::chrono::seconds(3);
            for (;;) {
                std::deque<Line> batch;
                std::size_t lost{};
                {
                    std::unique_lock lock(mutex);
                    wake.wait_until(lock, flushAt, [&] { return stop || !queue.empty(); });
                    if (stop && queue.empty()) break;
                    batch.swap(queue);
                    lost = dropped;
                    dropped = 0;
                }
                bool critical = false;
                for (const auto& line : batch) {
                    if (file == INVALID_HANDLE_VALUE) break;
                    if (!Write(line.text)) { Suspend(); break; }
                    critical |= line.level == LogLevel::Error;
                }
                if (file != INVALID_HANDLE_VALUE && lost &&
                    !Write(Format(LogLevel::Warn, "logger", "EVENTS_DROPPED", "count=" + std::to_string(lost))))
                    Suspend();
                if (Clock::now() >= flushAt || critical) {
                    if (file != INVALID_HANDLE_VALUE && !FlushFileBuffers(file)) Suspend();
                    flushAt = Clock::now() + std::chrono::seconds(3);
                }
            }
            if (file != INVALID_HANDLE_VALUE) FlushFileBuffers(file);
        } catch (...) {
            Suspend();
        }
    }

    void Suspend() noexcept {
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        file = INVALID_HANDLE_VALUE;
    }
};

SessionLog::SessionLog() = default;
SessionLog::~SessionLog() { Close(); }

void SessionLog::Start(const Path& module, const Path& game) noexcept {
    try {
        state_ = std::make_unique<State>();
        auto& s = *state_;
        const auto parent = module.parent_path();
        const auto stem = module.stem().wstring();
        s.folder = parent / L"logs";
        const auto pid = GetCurrentProcessId();
        FILETIME utc{};
        GetSystemTimePreciseAsFileTime(&utc);
        s.sid = std::to_string(pid) + "-" + std::to_string(utc.dwHighDateTime) + "-" + std::to_string(utc.dwLowDateTime);
        std::error_code ec;
        std::filesystem::create_directory(s.folder, ec);
        if (ec || (!s.Own(stem) && (GetLastError() != ERROR_ALREADY_EXISTS ||
                                    !s.Own(stem + L"." + std::to_wstring(pid)))) || !s.Rotate()) return;
        s.writer = std::thread([&s] { s.Run(); });
        Emit(LogLevel::Info, "startup", "SESSION_STARTED",
             "exe_build=" + GameBuild(game) + " mod=SuperJump_ThirdPalm mod_version=" SUPERJUMP_VERSION_TEXT +
             " pid=" + std::to_string(pid));

        // Each legacy file name is exact. The legacy writer may hold it open.
        if (s.family == stem) {
            for (unsigned i = 0; i <= 3; ++i) {
                const auto old = Segment(parent, stem, i);
                if (DeleteFileW(old.c_str())) continue;
                const auto error = GetLastError();
                if (!Missing(error))
                    Emit(LogLevel::Warn, "files", "LEGACY_CLEANUP_FAILED",
                         "stage=startup resource=Resource_Log reason=DeleteFailed code=" +
                         std::to_string(error) + " action=CONTINUE result=PRESERVED");
            }
        }

        const std::array<const wchar_t*, 4> extensions{L".asi", L".ini", L".dll", L".addon64"};
        const std::array<const char*, 4> events{"FILES_ASI", "FILES_INI", "FILES_DLL", "FILES_ADDON64"};
        std::array<std::vector<std::wstring>, 4> names;
        std::filesystem::directory_iterator it(parent, ec), end;
        const bool opened = !ec;
        for (; !ec && it != end; it.increment(ec)) {
            const auto path = it->path();
            const auto attr = GetFileAttributesW(path.c_str());
            if (attr == INVALID_FILE_ATTRIBUTES) { ec = std::error_code(GetLastError(), std::system_category()); break; }
            if (attr & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) continue;
            for (std::size_t i = 0; i < extensions.size(); ++i)
                if (_wcsicmp(path.extension().c_str(), extensions[i]) == 0) names[i].push_back(path.filename().wstring());
        }
        for (std::size_t i = 0; i < names.size(); ++i) {
            auto& list = names[i];
            std::ranges::sort(list, [](const auto& a, const auto& b) {
                const auto insensitive = _wcsicmp(a.c_str(), b.c_str());
                return insensitive < 0 || (insensitive == 0 && a < b);
            });
            std::string items;
            std::size_t omitted = 0;
            for (const auto& filename : list) {
                const auto clean = EscapeName(filename);
                if (clean.empty() || items.size() + clean.size() + 2 > inventoryLimit) { ++omitted; continue; }
                if (!items.empty()) items += ", ";
                items += clean;
            }
            const auto status = ec ? opened ? "PARTIAL" : "UNAVAILABLE" : "AVAILABLE";
            if (items.empty()) items = ec ? "(unavailable)" : omitted ? "(omitted)" : "(none)";
            auto fields = std::string("result=") + status + " names=\"" + items + "\" truncated=" +
                (omitted ? "true omitted=" + std::to_string(omitted) : "false");
            if (ec) fields += " stage=inventory resource=Resource_Files reason=EnumerationFailed code=" +
                std::to_string(ec.value()) + " action=CONTINUE";
            Emit(ec ? LogLevel::Warn : LogLevel::Info, "files", events[i], fields);
        }
        PrunePidFamilies(s.folder, stem, s.family);
    } catch (...) {
        // A failed logger startup leaves the game path available.
    }
}

void SessionLog::Emit(LogLevel level, std::string_view component, std::string_view event,
                      std::string_view fields) noexcept {
    if (!state_ || !state_->writer.joinable()) return;
    try {
        auto& s = *state_;
        auto message = s.Format(level, component, event, fields);
        std::lock_guard lock(s.mutex);
        if (level != LogLevel::Error && s.queue.size() >= queueLimit - 16) { ++s.dropped; return; }
        if (s.queue.size() == queueLimit) {
            const auto replace = std::ranges::find_if(s.queue, [](const State::Line& line) {
                return line.level != LogLevel::Error;
            });
            ++s.dropped;
            if (replace == s.queue.end()) return;
            s.queue.erase(replace);
        }
        s.queue.push_back({level, std::move(message)});
        s.wake.notify_one();
    } catch (...) {}
}

void SessionLog::Close() noexcept {
    if (!state_ || !state_->writer.joinable()) return;
    {
        std::lock_guard lock(state_->mutex);
        state_->stop = true;
    }
    state_->wake.notify_one();
    state_->writer.join();
    state_.reset();
}

} // namespace superjump
