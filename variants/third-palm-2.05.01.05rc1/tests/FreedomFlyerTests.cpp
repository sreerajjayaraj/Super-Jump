
#include "FreedomFlyerContract.h"
#include <array>
#include <algorithm>
#include <cassert>
#include <cstdio>

using namespace superjump;
using namespace superjump::freedom_flyer;

struct Fixture {
    static constexpr uintptr_t game = 0x140000000, site = game+0x4607B2F;
    static constexpr uintptr_t provider = 0x180000000, relay = game+0x5000000;
    std::array<std::uint8_t,0x1C000> module{};
    std::array<std::uint8_t,18> native{};
    std::array<std::uint8_t,128> cave{kCave};
    Fixture() {
        std::memcpy(module.data()+0xB232,kEmissionWitness.data(),kEmissionWitness.size());
        std::memcpy(native.data(),kStock.data(),kStock.size());
        native[8] = 0x41;
        for (const auto off : {0x180A0,0x180B0})
            for (std::size_t lane=0;lane<4;++lane) Put(off+lane*4,0x3F800000u);
    }
    template<typename T> void Put(std::size_t off,T value) {
        std::memcpy(module.data()+off,&value,sizeof(value));
    }
    void Install() {
        native[0]=0xE9;
        const auto jump=static_cast<std::int32_t>(relay-(site+5));
        std::memcpy(native.data()+1,&jump,4);
        native[5]=native[6]=native[7]=0x90;
        Put(0x18520,relay); Put(0x18528,site); Put(0x186E0,relay+0x800);
        Put(0x18550,std::uint8_t{1});
        const auto ptr=[&](std::size_t at,uintptr_t offset) {
            const auto resolved=provider+offset;
            std::memcpy(cave.data()+at,&resolved,sizeof(resolved));
        };
        ptr(0xB,0x186E8); ptr(0x18,0x180A0); ptr(0x25,0x180B0);
        ptr(0x32,0x18540); ptr(0x6F,0x18530);
        const auto back=static_cast<std::int32_t>(site+8-(relay+128));
        std::memcpy(cave.data()+0x7C,&back,4);
    }
    static bool Read(void* user,uintptr_t at,void* dest,std::size_t size) {
        const auto& f=*static_cast<Fixture*>(user);
        const auto copy=[&](const auto& bytes,uintptr_t base) {
            if (at<base || size>bytes.size() || at-base>bytes.size()-size) return false;
            std::memcpy(dest,bytes.data()+at-base,size);
            return true;
        };
        return copy(f.native,site)||copy(f.module,provider)||copy(f.cave,relay);
    }
    static bool Exec(void*,uintptr_t at,std::size_t n) { return at==relay && n==128; }
    boss::Reader Reader() { return {this,Read}; }
    Evidence Check(bool boost) { return Verify(Reader(),game,site,provider,boost,Exec); }
};

int main() {
    Fixture f;
    unsigned fail=0, count=0;
    const auto check=[&](bool pass,const char* desc) {
        ++count;
        if(!pass) { ++fail; std::printf("FAIL %s\n",desc); }
    };
    check(f.Check(true).verified && !f.Check(true).patched,"stock game while a neutral provider is present");
    f.Install();
    const auto proof=f.Check(true);
    check(proof.verified && proof.patched,"verified idle provider can coexist");
    View normalized{f.Reader(),Fixture::site,proof};
    std::array<std::uint8_t,18> sample{};
    check(normalized.Reader().Get(Fixture::site,sample) &&
          std::equal(kStock.begin(),kStock.end(),sample.begin()) && sample[8]==f.native[8],
          "normalization covers the eight approved provider bytes and leaves the next writer visible");
    f.Put(0x18544,0x3F000000u);
    check(f.Check(false).verified && !f.Check(true).verified,
          "active flight installs the independent group but suppresses jump mutation");
    f.Put(0x18544,0u); f.Put(0x180B4,0x3F800000u);
    check(f.Check(true).verified,"return to neutral permits native jump ownership");
    f.cave[0x7C]^=1;
    check(!f.Check(false).verified,"changed continuation cannot be normalized");
    f.cave[0x7C]^=1;
    f.Put(0x18528,Fixture::site+1);
    check(!f.Check(true).verified,"stale foreign publication fails");
    f.Put(0x18528,Fixture::site); f.native[6]^=1;
    check(!normalized.Reader().Get(Fixture::site,sample),"changed live entry cannot reuse the prior witness");
    f.native[6]^=1; f.module[0xB232]^=1;
    check(!f.Check(false).verified,"unrecognized provider cannot install the verified read view");
    std::printf("FreedomFlyer coexistence checks=%u failures=%u\n",count,fail);
    return fail ? 1 : 0;
}
