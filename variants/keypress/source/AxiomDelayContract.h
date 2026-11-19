#pragma once
#include "SuperJumpBossGuard.h"
#include <array>
#include <cstring>

namespace superjump::traversal::native {
inline constexpr uintptr_t kAxiomDelayRva=0x685B1C;
inline constexpr std::array<uint8_t,6> kStockAxiomDelay{0x0F,0x86,0x2A,0xFF,0xFF,0xFF};
inline constexpr std::array<uint8_t,6> kInstantAxiomDelay{0x90,0x90,0x90,0x90,0x90,0x90};

// Accept the exact alternate delay form only in a witness covering all six
// bytes. The caller still compares the entire native witness and validates
// its own hooks. Only the local read buffer is ever changed.
struct AxiomDelayView {
    boss::Reader native;
    uintptr_t gameBase;

    static bool Read(void* context,uintptr_t address,void* output,size_t size) {
        const auto& view=*static_cast<const AxiomDelayView*>(context);
        if (!view.native.Bytes(address,output,size)) return false;
        const auto delay=view.gameBase+kAxiomDelayRva;
        if (address<=delay && delay-address<=size &&
            size-(delay-address)>=kInstantAxiomDelay.size()) {
            auto* bytes=static_cast<uint8_t*>(output)+(delay-address);
            if (std::memcmp(bytes,kInstantAxiomDelay.data(),kInstantAxiomDelay.size())==0)
                std::memcpy(bytes,kStockAxiomDelay.data(),kStockAxiomDelay.size());
        }
        return true;
    }

    boss::Reader Wrapped() { return {this,Read}; }
};

template<class Block,size_t N>
constexpr bool WitnessesAxiomDelay(const Block (&blocks)[N]) {
    for (const auto& block:blocks) {
        if (block.rva>kAxiomDelayRva || kAxiomDelayRva-block.rva>block.size ||
            block.size-(kAxiomDelayRva-block.rva)<kStockAxiomDelay.size()) continue;
        const auto* bytes=block.bytes+(kAxiomDelayRva-block.rva);
        bool stock=true;
        for (size_t i=0;i<kStockAxiomDelay.size();++i)
            stock=stock && bytes[i]==kStockAxiomDelay[i];
        if (stock) return true;
    }
    return false;
}
}
