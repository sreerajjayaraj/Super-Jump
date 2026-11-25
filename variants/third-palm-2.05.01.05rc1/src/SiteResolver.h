#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace superjump {

struct CodeSection {
    std::uint32_t rva;
    std::span<const std::uint8_t> bytes;
};

struct CodeOwner {
    std::uint32_t begin;
    std::uint32_t end;
};

struct CodeSpan {
    std::uint32_t rva{};
    std::size_t length{};
};

struct NativeSites {
    std::uint32_t motion{};
    std::array<std::uint32_t, 2> palm{};
    std::uint32_t palmPublisher{};
    std::array<std::uint32_t, 2> palmReturnSites{};
    std::array<CodeSpan, 4> jumpCommitWitnesses{};
    bool HasJumpCommit() const noexcept { return jumpCommitWitnesses.front().length != 0; }
};

enum class ResolveError { None, InvalidImage, MotionMissing, MotionAmbiguous, PalmMissing, PalmAmbiguous };

struct SiteResolution {
    NativeSites sites;
    ResolveError error{ResolveError::None};
};

SiteResolution ResolveNativeSites(std::span<const CodeSection> sections,
                                  std::span<const CodeOwner> owners);

} // namespace superjump
