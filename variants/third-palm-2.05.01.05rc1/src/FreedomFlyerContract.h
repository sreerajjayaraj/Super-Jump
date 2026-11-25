#pragma once

#include <SuperJumpBossGuard.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <utility>

namespace superjump::freedom_flyer {
// Observed emission contract for the current installed provider. These
// immutable instructions identify the publisher, not the PE hash or version.
inline constexpr std::array<std::uint8_t, 35> kEmissionWitness{
    0x48, 0x8D, 0x05, 0x67, 0xCE, 0x00, 0x00, 0x48, 0x89, 0x42, 0x18, 0x48, 0x8D, 0x05, 0x6C, 0xCE,
    0x00, 0x00, 0x48, 0x89, 0x42, 0x25, 0x48, 0x8D, 0x05, 0xF1, 0xD2, 0x00, 0x00, 0x48, 0x89, 0x42,
    0x32, 0x41, 0x8B,
};
inline constexpr std::array<std::uint8_t, 8> kStock{
    0x0F, 0x28, 0xC6, 0xF3, 0x45, 0x0F, 0x5C, 0xC8
};
inline constexpr std::array<std::uint8_t, 128> kCave{
    0x0F, 0x28, 0xC6, 0xF3, 0x45, 0x0F, 0x5C, 0xC8, 0x50, 0x48, 0xB8, 0xE8, 0x86, 0x01, 0x80, 0x01,
    0x00, 0x00, 0x00, 0x48, 0x89, 0x18, 0x48, 0xB8, 0xA0, 0x80, 0x01, 0x80, 0x01, 0x00, 0x00, 0x00,
    0x0F, 0x59, 0x00, 0x48, 0xB8, 0xB0, 0x80, 0x01, 0x80, 0x01, 0x00, 0x00, 0x00, 0x0F, 0x59, 0x00,
    0x48, 0xB8, 0x40, 0x85, 0x01, 0x80, 0x01, 0x00, 0x00, 0x00, 0x0F, 0x58, 0x00, 0x8B, 0x83, 0x18,
    0x01, 0x00, 0x00, 0x89, 0x05, 0xBF, 0x07, 0x00, 0x00, 0x81, 0xBB, 0x18, 0x01, 0x00, 0x00, 0x9A,
    0x99, 0x19, 0x3F, 0x7E, 0x25, 0x8B, 0x83, 0x80, 0x00, 0x00, 0x00, 0x89, 0x05, 0x9F, 0x07, 0x00,
    0x00, 0x8B, 0x83, 0x88, 0x00, 0x00, 0x00, 0x89, 0x05, 0x97, 0x07, 0x00, 0x00, 0x48, 0xB8, 0x30,
    0x85, 0x01, 0x80, 0x01, 0x00, 0x00, 0x00, 0x0F, 0x58, 0x00, 0x58, 0xE9, 0x00, 0x00, 0x00, 0x00,
};
using Executable = bool (*)(void*, uintptr_t, std::size_t);
struct Snapshot {
    uintptr_t cave{}, published{}, scratch{};
    std::uint8_t installed{};
    bool operator==(const Snapshot&) const = default;
};

inline bool Neutral(const boss::Reader& r, uintptr_t module) {
    constexpr std::array<std::uint32_t,4> unity{0x3F800000,0x3F800000,0x3F800000,0x3F800000};
    constexpr std::array<std::uint32_t,4> zero{};
    std::array<std::uint32_t,4> value{};
    return r.Get(module+0x180A0,value) && value==unity &&
           r.Get(module+0x180B0,value) && value==unity &&
           r.Get(module+0x18530,value) && value==zero &&
           r.Get(module+0x18540,value) && value==zero;
}

struct Evidence {
    bool verified = false;
    bool patched = false;
    std::array<std::uint8_t,8> observed{};
};

inline Evidence Verify(const boss::Reader& r, uintptr_t base, uintptr_t site, uintptr_t module,
                       bool forBoost, Executable executable) {
    if (!module || !executable) return {};
    std::array<std::uint8_t,kEmissionWitness.size()> publisher{};
    std::array<std::uint8_t,8> patch{}, again{};
    if (!r.Get(module+0xB232,publisher) || publisher!=kEmissionWitness ||
        !r.Get(site,patch) || (forBoost && !Neutral(r,module))) return {};
    if (patch==kStock) {
        return r.Get(site,again) && again==patch && (!forBoost || Neutral(r,module))
            ? Evidence{true,false,patch} : Evidence{};
    }
    if (patch[0]!=0xE9 || patch[5]!=0x90 || patch[6]!=0x90 || patch[7]!=0x90) return {};
    std::int32_t forward{};
    std::memcpy(&forward,patch.data()+1,sizeof(forward));
    const auto cave=static_cast<uintptr_t>(static_cast<std::int64_t>(site+5)+forward);
    if (cave<0x10000 || cave>=0x800000000000ULL-kCave.size() ||
        !executable(reinterpret_cast<void*>(base),cave,kCave.size())) return {};
    const auto readPublication=[&](Snapshot& value) {
        return r.Get(module+0x18520,value.cave) && r.Get(module+0x18528,value.published) &&
               r.Get(module+0x186E0,value.scratch) && r.Get(module+0x18550,value.installed);
    };
    Snapshot first{}, second{};
    if (!readPublication(first) || first.cave!=cave || first.published!=site ||
        first.scratch!=cave+0x800 || first.installed!=1) return {};
    auto expected=kCave;
    constexpr std::array<std::pair<size_t,uintptr_t>,5> operands{{
        {0xB,0x186E8},{0x18,0x180A0},{0x25,0x180B0},{0x32,0x18540},{0x6F,0x18530}
    }};
    for (const auto& [offset,rva] : operands) {
        const auto address=module+rva;
        std::memcpy(expected.data()+offset,&address,sizeof(address));
    }
    const auto relative=static_cast<std::int64_t>(site+8)-static_cast<std::int64_t>(cave+expected.size());
    if (relative<INT32_MIN || relative>INT32_MAX) return {};
    const auto back=static_cast<std::int32_t>(relative);
    std::memcpy(expected.data()+0x7C,&back,sizeof(back));
    std::array<std::uint8_t,kCave.size()> actual{};
    if (!r.Get(cave,actual) || actual!=expected || !r.Get(site,again) || again!=patch ||
        !readPublication(second) || second!=first || !r.Get(cave,actual) || actual!=expected ||
        (forBoost && !Neutral(r,module))) return {};
    return {true,true,patch};
}

// Normalization is for the existing DLMM verifier's read view only. It
// cannot mask the following writer, and it never writes game or provider memory.
struct View {
    boss::Reader inner;
    uintptr_t site{};
    Evidence proof{};
    static bool Read(void* context, uintptr_t address, void* bytes, size_t size) {
        const auto& view=*static_cast<const View*>(context);
        if (!view.inner.Bytes(address,bytes,size)) return false;
        if (address==view.site && size>=kStock.size() && view.proof.patched) {
            if (std::memcmp(bytes,view.proof.observed.data(),kStock.size())) return false;
            std::memcpy(bytes,kStock.data(),kStock.size());
        }
        return true;
    }
    boss::Reader Reader() { return {this,Read}; }
};
} // namespace superjump::freedom_flyer
