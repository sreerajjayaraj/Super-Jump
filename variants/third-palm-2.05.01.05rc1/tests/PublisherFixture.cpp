#include "../src/PalmPublishPolicy.h"
#include "../src/HookGroup.h"

#include <Windows.h>
#include <safetyhook.hpp>

#include <array>
#include <cstring>
#include <iostream>

extern "C" {
void FixturePublisher();
std::uintptr_t FixtureCallFirst(std::uintptr_t, unsigned, unsigned, unsigned);
std::uintptr_t FixtureCallSecond(std::uintptr_t, unsigned, unsigned, unsigned);
std::uintptr_t FixtureCallOther(std::uintptr_t, unsigned, unsigned, unsigned);
extern std::uint8_t FixtureReturnFirst, FixtureReturnSecond;
extern std::uint8_t FixtureMotionSite;
unsigned FixtureMotion(std::uintptr_t, float*, unsigned);
}

namespace {
superjump::NativeSites sites;
std::uintptr_t imageBase;
std::array<unsigned, 2> observed{};
std::array<superjump::PalmPublication, 2> publications{};

void Capture(safetyhook::Context& context) {
    std::uintptr_t returnAddress{};
    std::memcpy(&returnAddress, reinterpret_cast<const void*>(context.rsp), sizeof(returnAddress));
    const auto publication = superjump::ClassifyPalmPublication(
        sites, imageBase, returnAddress, context.rcx, static_cast<std::uint32_t>(context.r9));
    if (publication) {
        const auto index = publication->collectorSite;
        publications[index] = *publication;
        ++observed[index];
    }
}

void Motion(safetyhook::Context& context) {
    if (context.rbx == 0x5000 && context.r13 == 0x5090)
        context.xmm6.f32[1] = 40;
}

superjump::Witness WitnessAt(const void* address, std::size_t length) {
    superjump::Witness witness{reinterpret_cast<std::uintptr_t>(address), {}, length};
    std::memcpy(witness.bytes.data(), address, length);
    return witness;
}
}

int main() {
    imageBase = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    sites.palmReturnSites = {
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&FixtureReturnFirst) - imageBase),
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&FixtureReturnSecond) - imageBase),
    };
    superjump::LoadedSites live{};
    live.original = {WitnessAt(&FixtureMotionSite, 5), WitnessAt(reinterpret_cast<void*>(&FixturePublisher), 7)};
    live.dependencies.fill(WitnessAt(&FixtureReturnFirst, 1));
    {
        auto edited = live;
        edited.original[1].bytes[0] ^= 1;
        superjump::HookGroup refused;
        if (refused.Install(edited, imageBase, safetyhook::Allocator::global(), {Motion, Capture}) !=
                superjump::HookOutcome::Changed || !live.original[0].Matches() || !live.original[1].Matches()) {
            std::cerr << "The incomplete group changed native code.\n";
            return 1;
        }
    }
    {
        superjump::HookGroup group;
        const auto outcome = group.Install(live, imageBase, safetyhook::Allocator::global(), {Motion, Capture});
        if (outcome != superjump::HookOutcome::Installed || !group.StillOwned()) {
            std::cerr << "SafetyHook did not install the isolated group: " << static_cast<unsigned>(outcome) << '\n';
            return 1;
        }
        const auto first = FixtureCallFirst(0x1000, 10, 20, 1);
        const auto second = FixtureCallSecond(0x2000, 30, 40, 0);
        const auto unrelated = FixtureCallOther(0x3000, 50, 60, 1);
        std::array<float, 4> delta{1, 2, 3, 4};
        if (first != 0x1001 || second != 0x2000 || unrelated != 0x3001 ||
            observed != std::array<unsigned, 2>{1, 1} ||
            publications[0].nativeState != 0x1000 || !publications[0].accepted ||
            publications[1].nativeState != 0x2000 || publications[1].accepted ||
            FixtureMotion(0x5000, delta.data(), 0) != 1 || delta != std::array<float, 4>{1, 40, 3, 4}) {
            std::cerr << "The group changed native results or misclassified a call.\n";
            return 1;
        }
        delta[1] = 2;
        if (FixtureMotion(0x5000, delta.data(), 2) != 2 || delta != std::array<float, 4>{1, 40, 3, 4}) {
            std::cerr << "Motion hook changed the taken branch.\n";
            return 1;
        }
        delta[1] = 2;
        if (FixtureMotion(0x5100, delta.data(), 2) != 2 || delta != std::array<float, 4>{1, 2, 3, 4}) {
            std::cerr << "Motion hook affected an unrelated body.\n";
            return 1;
        }
    }
    if (!live.original[0].Matches() || !live.original[1].Matches() ||
        FixtureCallFirst(0x4000, 0, 0, 1) != 0x4001 || observed[0] != 1) {
        std::cerr << "The isolated group did not restore the original bytes.\n";
        return 1;
    }
    std::cout << "Two-hook group passed: 5+7 byte spans, pre-commit refusal, both motion branches, "
                 "both publisher calls, unrelated caller/body, and restoration.\n";
}
