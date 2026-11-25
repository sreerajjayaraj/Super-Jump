#include "../src/BoostConfig.h"

#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

using superjump::BoostConfig;
using superjump::BoostConfigStatus;

namespace {

void Require(bool pass, const char* label) {
    if (!pass) throw std::runtime_error(label);
}

void Write(const std::filesystem::path& path, std::string_view content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    out.close();
    Require(static_cast<bool>(out), "write fixture INI");
}

std::string Utf16(std::string_view ascii) {
    std::string text{"\xFF\xFE"};
    for (const auto c : ascii) {
        text += c;
        text += '\0';
    }
    return text;
}

} // namespace

int main(int argc, char** argv) try {
    Require(argc == 2, "expected private fixture directory");
    const auto root = std::filesystem::path(argv[1]);
    std::filesystem::create_directories(root);
    const auto ini = root / "SuperJump_ThirdPalm.ini";
    const auto replacement = root / "replacement.ini";
    std::filesystem::remove(ini);
    std::filesystem::remove(replacement);
    BoostConfig config;
    config.SetPath(ini);
    Require(config.Poll(0) == BoostConfigStatus::Unavailable && config.Value() == 40.0f,
            "missing INI uses the RC3 default");
    Write(ini, "\xEF\xBB\xBF; comment\r\n[superJUMP]\r\n boosty = 24.5 ; comment\r\n");
    Require(!config.Poll(999) && config.Value() == 40.0f, "worker polls at most once per second");
    Require(config.Poll(1000) == BoostConfigStatus::Loaded && config.Value() == 24.5f,
            "UTF-8 BOM INI applies a lower boost during the same session");

    Write(replacement, Utf16("[SuperJump]\r\nBoostY=60.25\r\n"));
    std::filesystem::remove(ini);
    std::filesystem::rename(replacement, ini);
    Require(config.Poll(2000) == BoostConfigStatus::Loaded && config.Value() == 60.25f,
            "replacement UTF-16LE INI applies a higher boost");
    Require(!config.Poll(3000), "unchanged valid input is quiet");

    std::uint64_t tick = 4000;
    for (const auto* invalid : {"nan", "inf", "1e99", "30garbage", "", "0", "121", "-2"}) {
        Write(ini, std::string("[SuperJump]\nBoostY=") + invalid + "\n");
        config.Poll(tick);
        Require(config.Value() == 60.25f, "invalid edit preserves the last accepted boost");
        tick += 1000;
    }
    Write(ini, "[Other]\nBoostY=95\n");
    config.Poll(tick);
    Require(config.Value() == 60.25f, "unrelated section cannot change the boost");
    tick += 1000;
    Write(ini, "[SuperJump]\nBoostY=22\nBoostY=95\n");
    config.Poll(tick);
    Require(config.Value() == 60.25f, "duplicate key preserves the prior value");
    tick += 1000;
    Write(ini, "[SuperJump]\nBoostY=36\n" + std::string(4096, 'a'));
    config.Poll(tick);
    Require(config.Value() == 60.25f, "oversize file preserves the prior value");
    tick += 1000;
    std::filesystem::remove(ini);
    Require(config.Poll(tick) == BoostConfigStatus::Unavailable && config.Value() == 60.25f,
            "temporary removal during a save preserves the active boost");
    tick += 1000;
    Write(ini, "[SuperJump]\nBoostY=40.0\n");
    Require(config.Poll(tick) == BoostConfigStatus::Loaded && config.Value() == 40.0f,
            "saving the default restores the RC3 boost in the same session");
    std::filesystem::remove(ini);
    std::puts("Boost configuration passed: live saves, default, syntax, bounds, encoding, replacement and throttling.");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
