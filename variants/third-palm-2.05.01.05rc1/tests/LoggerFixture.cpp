#include "../src/SessionLog.h"
#include "../src/ModVersion.h"

#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

namespace {
namespace fs = std::filesystem;
std::string Read(const fs::path& filename) {
    std::ifstream file(filename, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}

bool Has(const std::string& content, std::string_view token) {
    return content.find(token) != std::string::npos;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) return 2;
    const auto root = fs::path(argv[1]) / (L"logger-" + std::to_wstring(GetCurrentProcessId()) +
        L"-" + std::to_wstring(GetTickCount64()));
    fs::create_directories(root / "child");
    const auto module = root / L"SuperJump_ThirdPalm.asi";
    const auto games = fs::path(argv[2]);
    constexpr std::string_view base = "SuperJump_ThirdPalm";
    std::ofstream(module) << "fixture";
    std::ofstream(root / L"Z-test.ini") << "fixture";
    std::ofstream(root / L"a-test.ini") << "fixture";
    std::ofstream(root / L"child" / L"hidden.dll") << "fixture";
    std::ofstream(root / L"keep.log") << "must survive";
    const auto legacy = root / (std::string(base) + ".01.log");
    std::ofstream(legacy) << "old fixture";
    superjump::SessionLog first, second;
    first.Start(module, games);
    second.Start(module, games);
    first.Emit(superjump::LogLevel::Info, "test", "FIRST_FIXTURE");
    second.Emit(superjump::LogLevel::Error, "test", "SECOND_FIXTURE",
        "stage=test resource=Group_A reason=Fixture code=none action=CONTINUE result=EXPECTED");
    second.Close();
    first.Close();

    superjump::SessionLog later;
    later.Start(module, games);
    later.Emit(superjump::LogLevel::Info, "test", "NEXT_FIXTURE");
    later.Close();

    const auto active = Read(root / "logs" / (std::string(base) + ".log"));
    const auto past = Read(root / "logs" / (std::string(base) + ".01.log"));
    const auto pid = Read(root / "logs" / (std::string(base) + "." +
        std::to_string(GetCurrentProcessId()) + ".log"));
    if (!Has(active, "SESSION_STARTED exe_build=1.0.0.3732") ||
        !Has(active, "mod_version=" SUPERJUMP_VERSION_TEXT) ||
        !Has(active, "event=FILES_INI result=AVAILABLE names=\"a-test.ini, Z-test.ini\"") ||
        !Has(active, "event=FILES_ASI result=AVAILABLE names=\"SuperJump_ThirdPalm.asi\"") ||
        !Has(active, "event=FILES_DLL result=AVAILABLE names=\"(none)\"") ||
        !Has(active, "event=FILES_ADDON64 result=AVAILABLE names=\"(none)\"") ||
        !Has(active, "event=NEXT_FIXTURE") || !Has(past, "event=FIRST_FIXTURE") ||
        !Has(pid, "event=SECOND_FIXTURE") || Has(past, "event=SECOND_FIXTURE") ||
        fs::exists(legacy) || Read(root / "keep.log") != "must survive") {
        std::cerr << "Logger fixture failed: " << root.string() << '\n';
        return 1;
    }
    std::cout << "Logger fixture passed: version, four inventories, startup rotation, PID ownership, and legacy cleanup.\n";
}
