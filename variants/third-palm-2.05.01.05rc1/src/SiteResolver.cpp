#include "SiteResolver.h"

#include <algorithm>
#include <bit>
#include <iterator>
#include <limits>
#include <optional>
#include <tuple>
#include <vector>

namespace superjump {
namespace {

using Byte = std::uint8_t;
using Rva = std::uint32_t;

constexpr std::array<Byte, 10> motionAnchor{0x83,0x7C,0x24,0x30,0x02,0x0F,0x28,0x75,0x90,0x74};
constexpr std::array<Byte, 3> motionFallthrough{0x0F,0x28,0xC6};
// The taken branch restores the receiver and then dispatches its virtual method.
constexpr std::array<Byte, 13> motionTaken{0x48,0x8B,0x8D,0xD0,0x03,0x00,0x00,0x48,0x8B,0x01,0xFF,0x50,0x10};
constexpr std::array<Byte, 8> palmAcceptCall{0x0F,0xB6,0x45,0x6F,0x48,0x8B,0xCE,0xE8};
// Callback observes the original registers before the arguments are staged on the stack.
constexpr std::array<Byte, 27> palmPublishCall{
    0x4C,0x89,0x7C,0x24,0x28,0x4C,0x89,0x74,0x24,0x20,0x44,0x0F,0xB6,0xC8,
    0x44,0x0F,0xB6,0x45,0x67,0x0F,0xB6,0x55,0x6F,0x48,0x8B,0xCE,0xE8
};
// Identity witnesses for the two callees. The later profile checks still own object/layout validation.
constexpr std::array<Byte, 11> acceptEntry{0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57};
constexpr std::array<Byte, 15> publishEntry{0x4C,0x8B,0xDC,0x49,0x89,0x5B,0x08,0x49,0x89,0x6B,0x10,0x49,0x89,0x73,0x18};

bool Contains(const CodeOwner& owner, Rva start, std::size_t count) {
    return start >= owner.begin && start < owner.end && count <= owner.end - start;
}

const CodeOwner* OwnerAt(std::span<const CodeOwner> owners, Rva start, std::size_t count) {
    const auto next = std::upper_bound(owners.begin(), owners.end(), start,
        [](Rva address, const CodeOwner& entry) { return address < entry.begin; });
    if (next == owners.begin()) return nullptr;
    const auto& owner = *std::prev(next);
    return Contains(owner, start, count) ? &owner : nullptr;
}

std::span<const Byte> Read(std::span<const CodeSection> sections, Rva start, std::size_t count) {
    for (const auto& section : sections) {
        if (start < section.rva) break;
        const auto offset = start - section.rva;
        if (offset <= section.bytes.size() && count <= section.bytes.size() - offset)
            return section.bytes.subspan(offset, count);
    }
    return {};
}

template<std::size_t N>
bool Matches(std::span<const CodeSection> sections, Rva at, const std::array<Byte, N>& expected) {
    return std::ranges::equal(Read(sections, at, N), expected);
}

std::optional<Rva> RelativeTarget(Rva next, std::int64_t displacement) {
    const auto target = std::int64_t(next) + displacement;
    if (target < 0 || target > std::numeric_limits<Rva>::max()) return std::nullopt;
    return static_cast<Rva>(target);
}

std::optional<Rva> ReadCallTarget(std::span<const CodeSection> sections, Rva operand) {
    const auto raw = Read(sections, operand, 4);
    if (raw.empty()) return std::nullopt;
    const auto value = std::uint32_t(raw[0]) | (std::uint32_t(raw[1]) << 8) |
                       (std::uint32_t(raw[2]) << 16) | (std::uint32_t(raw[3]) << 24);
    return RelativeTarget(operand + 4, std::bit_cast<std::int32_t>(value));
}

template<std::size_t N, class Visitor>
void Find(std::span<const CodeSection> sections, const std::array<Byte, N>& pattern, Visitor visit) {
    for (const auto& section : sections) {
        auto from = section.bytes.begin();
        for (;;) {
            const auto match = std::search(from, section.bytes.end(), pattern.begin(), pattern.end());
            if (match == section.bytes.end()) break;
            visit(section.rva + static_cast<Rva>(match - section.bytes.begin()));
            from = std::next(match);
        }
    }
}

std::array<CodeSpan, 4> VerifyJumpCommit(std::span<const CodeSection> sections,
                                       std::span<const CodeOwner> owners, Rva entry) {
    // RDI receives the same movement state passed to the publisher. AL reports
    // success only after the armed request is consumed and its launch is stored.
    constexpr std::array<Byte, 11> receiver{0x57,0x48,0x81,0xEC,0xC0,0,0,0,0x48,0x8B,0xF9};
    constexpr std::array<Byte, 26> gates{
        0x80,0xBF,0x80,0x03,0,0,0,0x0F,0x84,0xED,0x01,0,0,
        0x80,0xBF,0xAD,0x03,0,0,0,0x0F,0x85,0xE0,0x01,0,0
    };
    constexpr std::array<Byte, 10> launch{0xC5,0xFA,0x11,0x87,0xA0,0x03,0,0,0x33,0xF6};
    constexpr std::array<Byte, 19> result{
        0xC6,0x87,0x80,0x03,0,0,0,0x89,0xB7,0x8C,0x03,0,0,0xB0,0x01,0xEB,0x02,0x32,0xC0
    };
    constexpr Rva gatesFromEntry = 0x95, launchFromEntry = 0x13D, resultFromEntry = 0x27E;
    if (!OwnerAt(owners, entry, resultFromEntry + result.size()) ||
        !Matches(sections, entry + 0xA, receiver) ||
        !Matches(sections, entry + gatesFromEntry, gates) ||
        !Matches(sections, entry + launchFromEntry, launch) ||
        !Matches(sections, entry + resultFromEntry, result)) return {};
    return {{{entry, acceptEntry.size() - 1 + receiver.size()}, {entry + gatesFromEntry, gates.size()},
             {entry + launchFromEntry, launch.size()}, {entry + resultFromEntry, result.size()}}};
}

struct PalmCandidate {
    Rva site;
    Rva owner;
    Rva accept;
    Rva publish;
    Rva returnSite;
};

} // namespace

SiteResolution ResolveNativeSites(std::span<const CodeSection> sections,
                                  std::span<const CodeOwner> owners) {
    constexpr auto maxRva = std::numeric_limits<Rva>::max();
    if (sections.empty() || owners.empty()) return {{}, ResolveError::InvalidImage};
    Rva previous = 0;
    for (const auto& section : sections) {
        if (section.rva < previous || section.bytes.empty() || section.bytes.size() > maxRva - section.rva)
            return {{}, ResolveError::InvalidImage};
        previous = section.rva + static_cast<Rva>(section.bytes.size());
    }
    previous = 0;
    for (const auto& owner : owners) {
        if (owner.begin < previous || owner.begin >= owner.end) return {{}, ResolveError::InvalidImage};
        previous = owner.end;
    }

    NativeSites sites;
    std::size_t motions = 0;
    Find(sections, motionAnchor, [&](Rva anchor) {
        constexpr std::size_t spanSize = motionAnchor.size() + 1 + motionFallthrough.size();
        const auto bytes = Read(sections, anchor, spanSize);
        const auto* owner = OwnerAt(owners, anchor, spanSize);
        if (bytes.empty() || !owner ||
            !std::ranges::equal(bytes.last(motionFallthrough.size()), motionFallthrough)) return;
        const auto site = anchor + static_cast<Rva>(motionAnchor.size()) - 1;
        const auto target = RelativeTarget(site + 2, std::bit_cast<std::int8_t>(bytes[motionAnchor.size()]));
        if (!target || *target < site + 5 || !Contains(*owner, *target, motionTaken.size()) ||
            !Matches(sections, *target, motionTaken)) return;
        sites.motion = site;
        ++motions;
    });
    if (motions != 1) return {{}, motions ? ResolveError::MotionAmbiguous : ResolveError::MotionMissing};

    std::vector<PalmCandidate> candidates;
    Find(sections, palmPublishCall, [&](Rva site) {
        constexpr Rva prior = static_cast<Rva>(palmAcceptCall.size()) + 4;
        constexpr auto spanSize = prior + palmPublishCall.size() + 4;
        if (site < prior || Read(sections, site - prior, spanSize).empty()) return;
        const auto* owner = OwnerAt(owners, site - prior, spanSize);
        if (!owner || !Matches(sections, site - prior, palmAcceptCall)) return;
        const auto accept = ReadCallTarget(sections, site - 4);
        const auto publish = ReadCallTarget(sections, site + static_cast<Rva>(palmPublishCall.size()));
        if (!accept || !publish || *accept == *publish) return;
        const auto* acceptOwner = OwnerAt(owners, *accept, acceptEntry.size());
        const auto* publishOwner = OwnerAt(owners, *publish, publishEntry.size());
        if (!acceptOwner || acceptOwner->begin != *accept || !publishOwner || publishOwner->begin != *publish ||
            !Matches(sections, *accept, acceptEntry) || !Matches(sections, *publish, publishEntry)) return;
        const auto returnSite = site + static_cast<Rva>(palmPublishCall.size()) + 4;
        candidates.push_back({site, owner->begin, *accept, *publish, returnSite});
    });
    const auto identity = [](const PalmCandidate& c) { return std::tuple{c.owner, c.accept, c.publish}; };
    std::ranges::stable_sort(candidates, {}, identity);
    for (auto first = candidates.begin(); first != candidates.end();) {
        const auto end = std::find_if(first, candidates.end(),
            [&](const auto& candidate) { return identity(candidate) != identity(*first); });
        const auto count = end - first;
        if (count > 2 || (count == 2 && sites.palmPublisher)) return {{}, ResolveError::PalmAmbiguous};
        if (count == 2) {
            sites.palm = {first[0].site, first[1].site};
            sites.palmPublisher = first->publish;
            sites.palmReturnSites = {first[0].returnSite, first[1].returnSite};
            sites.jumpCommitWitnesses = VerifyJumpCommit(sections, owners, first->accept);
        }
        first = end;
    }
    if (!sites.palmPublisher) return {{}, ResolveError::PalmMissing};
    return {sites, ResolveError::None};
}

} // namespace superjump
