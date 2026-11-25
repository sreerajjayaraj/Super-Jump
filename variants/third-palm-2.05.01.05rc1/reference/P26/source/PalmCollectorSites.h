#pragma once

#include "MotionProducerLocator.h"
#include <cstring>

namespace superjump::palm {

template<class Reader,class OwnsCode>
std::array<uint32_t,2> LocateCollectorSites(const Reader& reader,uintptr_t base,OwnsCode ownsCode) {
    constexpr size_t ownerSize=0xCD4;
    constexpr std::array<size_t,2> offsets{0x1F1,0xA3F};
    constexpr size_t prelude=12;
    constexpr std::array<uint8_t,8> inputs{0x0F,0xB6,0x45,0x6F,0x48,0x8B,0xCE,0xE8};
    constexpr std::array<uint8_t,27> bridge{
        0x4C,0x89,0x7C,0x24,0x28,0x4C,0x89,0x74,0x24,0x20,0x44,0x0F,0xB6,0xC8,
        0x44,0x0F,0xB6,0x45,0x67,0x0F,0xB6,0x55,0x6F,0x48,0x8B,0xCE,0xE8};
    constexpr std::array<uint8_t,18> acceptPrologue{
        0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x81,0xEC,0xC0,0,0,0};
    const auto owner=motion::UniqueOwner(reader,base,ownerSize,[&](uintptr_t start) {
        std::array<uintptr_t,2> endpoints{};
        for (const auto offset:offsets) {
            const auto site=start+offset;
            std::array<uint8_t,prelude+bridge.size()+4> bytes{};
            if (!ownsCode(site-prelude,bytes.size()) ||
                !reader.Bytes(site-prelude,bytes.data(),bytes.size()) ||
                !std::equal(inputs.begin(),inputs.end(),bytes.begin()) ||
                !std::equal(bridge.begin(),bridge.end(),bytes.begin()+prelude)) return false;
            int32_t accepted=0,publish=0;
            std::memcpy(&accepted,bytes.data()+inputs.size(),sizeof(accepted));
            std::memcpy(&publish,bytes.data()+prelude+bridge.size(),sizeof(publish));
            const auto acceptAddress=site+accepted;
            const auto publishAddress=site+bridge.size()+4+publish;
            if (endpoints[0] && (endpoints[0]!=acceptAddress || endpoints[1]!=publishAddress))
                return false;
            endpoints={acceptAddress,publishAddress};
        }
        std::array<uint8_t,acceptPrologue.size()> accept{};
        return endpoints[0]!=endpoints[1] && ownsCode(endpoints[0],accept.size()) &&
               ownsCode(endpoints[1],1) && reader.Bytes(endpoints[0],accept.data(),accept.size()) &&
               accept==acceptPrologue;
    });
    if (!owner) return {};
    return {static_cast<uint32_t>(owner-base+offsets[0]),
            static_cast<uint32_t>(owner-base+offsets[1])};
}
}

