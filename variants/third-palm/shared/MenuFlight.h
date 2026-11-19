#pragma once
#include "../compat/MemoryRegion.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <vector>

namespace superjump::menu60 {
using Reader = boss::Reader;
struct FlightWriterProfile {
    std::array<uintptr_t,6> slots;
    uintptr_t published, patch, target, sharedBridge;
};
struct ModeEntry {
    uintptr_t target=0,original=0,entry=0,rowRva=0;
    std::array<uint32_t,3> row{};
};
struct ModeOwner {
    ModeEntry legacy{},typed{};
    std::array<uint8_t,51> legacyBytes{};
    std::array<uint8_t,26> typedBytes{};
};
struct AdaptiveLayout {
    FlightWriterProfile flight{};
    ModeOwner mode{};
    uintptr_t jumpMultiply=0;
    std::array<uint8_t,12> jumpBytes{};
};

struct Profile {
    uintptr_t flightEnabled, flightHelper;
    std::array<uint8_t,26> flightBytes;
    uintptr_t jumpEnabled, jumpGate;
    std::array<uint8_t,36> jumpBytes;
    uintptr_t locomotionTarget, locomotionOriginal, locomotionHandler;
    std::array<uint8_t,32> locomotionBytes;
    uintptr_t stepperTarget, stepperOriginal, stepperHandler, stepperHandlerRow;
    std::array<uint8_t,53> stepperBytes;
    std::array<uint32_t,3> stepperRow;
    uintptr_t movementReady;
    const AdaptiveLayout* adaptive=nullptr;
};

#include "Menu60Profiles.generated.h"
#include "Menu601Profiles.generated.h"
#include "Menu604Profiles.generated.h"

struct Provider {
    uintptr_t module=0;
    const Profile* profile=nullptr;
    explicit operator bool() const { return module>1 && profile; }
};

// A retained HMODULE and its immutable profile are published by a single worker.
// Readers must acquire the module before reading the profile.
class PublishedProvider {
    std::atomic<uintptr_t> module_{0};
    std::atomic<const Profile*> profile_{nullptr};
public:
    Provider Load() const {
        const auto module=module_.load(std::memory_order_acquire);
        return {module,module>1 ? profile_.load(std::memory_order_relaxed) : nullptr};
    }
    void Publish(Provider provider) {
        if (provider) profile_.store(provider.profile,std::memory_order_relaxed);
        module_.store(provider.module,std::memory_order_release);
    }
};

inline bool FlightInactive(const Reader& reader,uintptr_t enabledAddress,bool requireDisabled) {
    if (!enabledAddress || enabledAddress%alignof(uint32_t)) return false;
    std::array<uint32_t,2> first{}, second{};
    return reader.Get(enabledAddress,first) && reader.Get(enabledAddress,second) &&
           first==second && first[0]<=(requireDisabled?0u:1u) && first[1]==0;
}

inline constexpr uintptr_t kStepper=0x39F3390;
inline constexpr std::array<uint8_t,7> kStepperPrefix{0x48,0x8B,0xC4,0x48,0x89,0x58,0x10};
inline constexpr std::array<uint8_t,6> kAbsoluteJump{0xFF,0x25,0,0,0,0};
inline constexpr size_t kRelJump=5, kAbsoluteSpan=14;
inline constexpr size_t kRelayOffset=kStepperPrefix.size()+kAbsoluteSpan;
using StepperBridge=std::array<uint8_t,kRelayOffset+kAbsoluteSpan>;

inline bool JumpInactive(const Reader& r,Provider menu) {
    uint32_t first=1, second=1;
    const auto at=menu.module+menu.profile->jumpEnabled;
    return r.Get(at,first) && r.Get(at,second) && first==0 && second==0;
}

inline bool FlightAllowsBoost(const Reader& r,Provider menu) {
    if (!menu.module) return true;
    if (!menu) return false;
    // 6.0 retains the earlier explicit-off behavior. Later exact helpers
    // distinguish an armed toggle from engagement, and DeltaReady proves
    // the resident writer is neutral before delivery.
    return FlightInactive(r,menu.module+menu.profile->flightEnabled,menu.profile==&kUpdated);
}

inline bool AllowsBoost(const Reader& r,Provider menu) {
    return !menu.module || (FlightAllowsBoost(r,menu) && JumpInactive(r,menu));
}

inline bool WritableSlot(uintptr_t owner,uintptr_t at,size_t size) {
    return native_memory::Region(at,size,owner,MEM_IMAGE,PAGE_READWRITE) ||
           native_memory::Region(at,size,owner,MEM_IMAGE,PAGE_WRITECOPY);
}

template<class T>
bool MatchesImage(const Reader& r,uintptr_t owner,uintptr_t rva,const T& expected,DWORD protection) {
    T actual{};
    return native_memory::Region(owner+rva,sizeof(actual),owner,MEM_IMAGE,protection) &&
           r.Get(owner+rva,actual) && actual==expected;
}

inline bool MovementSlots(const Reader& r,Provider menu) {
    const auto owner=menu.module;
    const auto& p=*menu.profile;
    return WritableSlot(owner,owner+p.flightEnabled,2*sizeof(uint32_t)) &&
           WritableSlot(owner,owner+p.jumpEnabled,sizeof(uint32_t)) &&
           WritableSlot(owner,owner+p.stepperTarget,sizeof(uintptr_t)) &&
           WritableSlot(owner,owner+p.stepperOriginal,sizeof(uintptr_t)) &&
           WritableSlot(owner,owner+p.movementReady,2) &&
           WritableSlot(owner,owner+p.locomotionTarget,sizeof(uintptr_t)) &&
           WritableSlot(owner,owner+p.locomotionOriginal,sizeof(uintptr_t)) &&
           MatchesImage(r,owner,p.flightHelper,p.flightBytes,PAGE_EXECUTE_READ) &&
           MatchesImage(r,owner,p.locomotionHandler,p.locomotionBytes,PAGE_EXECUTE_READ) &&
           MatchesImage(r,owner,p.jumpGate,p.jumpBytes,PAGE_EXECUTE_READ) &&
           MatchesImage(r,owner,p.stepperHandler,p.stepperBytes,PAGE_EXECUTE_READ) &&
           MatchesImage(r,owner,p.stepperHandlerRow,p.stepperRow,PAGE_READONLY);
}

inline bool PrivateExecutable(void*,uintptr_t at,size_t size) {
    MEMORY_BASIC_INFORMATION region{};
    if (VirtualQuery(reinterpret_cast<void*>(at),&region,sizeof(region))!=sizeof(region)) return false;
    const auto owner=reinterpret_cast<uintptr_t>(region.AllocationBase);
    return native_memory::Region(at,size,owner,MEM_PRIVATE,PAGE_EXECUTE_READ) ||
           native_memory::Region(at,size,owner,MEM_PRIVATE,PAGE_EXECUTE_READWRITE);
}

inline bool JumpMatches(const uint8_t* op,uintptr_t destination) {
    uintptr_t target=0;
    memcpy(&target,op+kAbsoluteJump.size(),sizeof(target));
    return target==destination && !memcmp(op,kAbsoluteJump.data(),kAbsoluteJump.size());
}

struct StepperSnapshot {
    uintptr_t target=0,original=0;
    std::array<uint8_t,kStepperPrefix.size()> entry{};
    StepperBridge bridge{};
    bool operator==(const StepperSnapshot&) const = default;
};

inline bool StepperContract(const StepperSnapshot& s,uintptr_t entry,Provider menu) {
    if (s.target!=entry || s.entry[0]!=0xE9 ||
        memcmp(s.entry.data()+kRelJump,kStepperPrefix.data()+kRelJump,kStepperPrefix.size()-kRelJump) ||
        memcmp(s.bridge.data(),kStepperPrefix.data(),kStepperPrefix.size())) return false;
    int32_t displacement=0;
    memcpy(&displacement,s.entry.data()+1,sizeof(displacement));
    return static_cast<int64_t>(entry+kRelJump)+displacement==static_cast<int64_t>(s.original+kRelayOffset) &&
           JumpMatches(s.bridge.data()+kStepperPrefix.size(),entry+kStepperPrefix.size()) &&
           JumpMatches(s.bridge.data()+kRelayOffset,menu.module+menu.profile->stepperHandler);
}

// Compose the exact Menu stepper lease with the existing SAF read view.
// The original bytes exist only in the validation copy, never in the game.
struct TraversalView {
    Reader native;
    Reader raw;
    uintptr_t entry;
    Provider menu;
    bool (*executable)(void*,uintptr_t,size_t)=PrivateExecutable;
    void* executableContext=nullptr;

    bool Observe(StepperSnapshot& s) const {
        return raw.Get(menu.module+menu.profile->stepperTarget,s.target) && s.target==entry &&
               raw.Get(menu.module+menu.profile->stepperOriginal,s.original) &&
               s.original>=0x10000 && s.original<0x800000000000ULL-s.bridge.size() &&
               executable(executableContext,s.original,s.bridge.size()) &&
               raw.Get(entry,s.entry) && raw.Get(s.original,s.bridge) &&
               StepperContract(s,entry,menu);
    }

    static bool Read(void* opaque,uintptr_t at,void* output,size_t n) {
        auto& v=*static_cast<TraversalView*>(opaque);
        if (!v.native.Bytes(at,output,n)) return false;
        if (at!=v.entry || n<kStepperPrefix.size() ||
            !memcmp(output,kStepperPrefix.data(),kStepperPrefix.size())) return true;
        StepperSnapshot a{}, b{};
        if (!v.menu || !v.Observe(a) || !v.Observe(b) || a!=b ||
            memcmp(output,a.entry.data(),a.entry.size())) return false;
        memcpy(output,kStepperPrefix.data(),kStepperPrefix.size());
        return true;
    }
};

inline constexpr uintptr_t kLocomotion=0x46027C0;
inline constexpr std::array<uint8_t,7> kLocomotionPrefix{0x48,0x8B,0xC4,0x4C,0x89,0x48,0x20};

inline bool LocomotionReady(const Reader& r,uintptr_t base,Provider menu,
                            bool (*executable)(void*,uintptr_t,size_t)=PrivateExecutable) {
    std::array<uint8_t,7> locomotion{}, stepper{};
    if (!r.Get(base+kLocomotion,locomotion) || !r.Get(base+kStepper,stepper))
        return false;
    const bool stockLocomotion=locomotion==kLocomotionPrefix;
    const bool stockStepper=stepper==kStepperPrefix;
    const auto flagsAre=[&](uint8_t expected) {
        std::array<uint8_t,2> flags{};
        return r.Get(menu.module+menu.profile->movementReady,flags) && flags[0]==expected && flags[1]==expected;
    };
    if (stockLocomotion && stockStepper) {
        std::array<uint8_t,7> nextLocomotion{}, nextStepper{};
        return (!menu || flagsAre(0)) &&
               r.Get(base+kLocomotion,nextLocomotion) && nextLocomotion==locomotion &&
               r.Get(base+kStepper,nextStepper) && nextStepper==stepper &&
               (!menu || flagsAre(0));
    }
    if (!menu || stockLocomotion || stockStepper || !flagsAre(1)) return false;
    TraversalView view{r,r,base+kStepper,menu,executable};
    StepperSnapshot stepperFirst{}, stepperSecond{};
    const auto entry=base+kLocomotion;
    const auto observe=[&](StepperSnapshot& snapshot) {
        if (!r.Get(menu.module+menu.profile->locomotionTarget,snapshot.target) || snapshot.target!=entry ||
            !r.Get(menu.module+menu.profile->locomotionOriginal,snapshot.original) ||
            snapshot.original<0x10000 || snapshot.original>=0x800000000000ULL-snapshot.bridge.size() ||
            !executable(nullptr,snapshot.original,snapshot.bridge.size()) ||
            !r.Get(entry,snapshot.entry) || !r.Get(snapshot.original,snapshot.bridge))
            return false;
        const auto& bytes=snapshot.entry;
        if (bytes[0]!=0xE9 || memcmp(bytes.data()+kRelJump,kLocomotionPrefix.data()+kRelJump,2) ||
            memcmp(snapshot.bridge.data(),kLocomotionPrefix.data(),kLocomotionPrefix.size()))
            return false;
        int32_t displacement=0;
        memcpy(&displacement,bytes.data()+1,sizeof(displacement));
        return static_cast<int64_t>(entry+kRelJump)+displacement==static_cast<int64_t>(snapshot.original+kRelayOffset) &&
               JumpMatches(snapshot.bridge.data()+kLocomotionPrefix.size(),entry+kLocomotionPrefix.size()) &&
               JumpMatches(snapshot.bridge.data()+kRelayOffset,menu.module+menu.profile->locomotionHandler);
    };
    StepperSnapshot a{}, b{};
    return view.Observe(stepperFirst) && view.Observe(stepperSecond) &&
           stepperFirst==stepperSecond && stepperFirst.entry==stepper &&
           observe(a) && observe(b) && a==b && a.entry==locomotion && flagsAre(1);
}
inline constexpr uintptr_t kDeltaRva=0x460358F, kWriterRva=kDeltaRva+8;
inline constexpr std::array<uint8_t,18> kStockDelta{
    0x0F,0x28,0xC6,0xF3,0x45,0x0F,0x5C,0xC8,
    0x41,0x0F,0x58,0x45,0x00,0x41,0x0F,0x11,0x45,0x00
};

inline bool StockDelta(const Reader& r,uintptr_t base) {
    std::array<uint8_t,kStockDelta.size()> bytes{};
    return r.Get(base+kDeltaRva,bytes) && bytes==kStockDelta;
}

#include "Menu60FlightCave.generated.h"

inline bool FlightCaveRegion(void* game,uintptr_t cave,size_t size) {
    MEMORY_BASIC_INFORMATION region{};
    if (VirtualQuery(reinterpret_cast<void*>(cave),&region,sizeof(region))!=sizeof(region)) return false;
    if (native_memory::Region(cave,size,reinterpret_cast<uintptr_t>(game),MEM_IMAGE,PAGE_EXECUTE_READ)) return true;
    return region.RegionSize==0x1000 && native_memory::Region(cave,size,cave,MEM_PRIVATE,PAGE_EXECUTE_READWRITE);
}

inline constexpr FlightWriterProfile kUpdatedWriter{
    kFlightCaveMenuSlots, 0x69E808, 0x69E810, 0x69E820, 0x69E828
};
#include "Menu601FlightCave.generated.h"
#include "Menu604FlightCave.generated.h"

inline const FlightWriterProfile* FlightWriterFor(const Profile* profile) {
    if (profile==&kUpdated) return &kUpdatedWriter;
    if (profile==&kMenu601) return &kMenu601Writer;
    if (profile==&kMenu604) return &kMenu604Writer;
    return profile && profile->adaptive ? &profile->adaptive->flight : nullptr;
}

inline bool NeutralFlight(const Reader& r,Provider menu,const FlightWriterProfile& writer) {
    constexpr std::array<uint32_t,4> unity{0x3F800000,0x3F800000,0x3F800000,0x3F800000}, zero{};
    std::array<uint32_t,4> multiplier{}, add{};
    std::array<uint32_t,3> vertical{};
    return FlightAllowsBoost(r,menu) &&
           r.Get(menu.module+writer.slots[2],multiplier) && multiplier==unity &&
           r.Get(menu.module+writer.slots[3],add) && add==zero &&
           r.Get(menu.module+writer.slots[5],vertical) &&
           vertical==std::array<uint32_t,3>{0,0x3DCCCCCD,0} && FlightAllowsBoost(r,menu);
}

struct WriterSnapshot {
    uintptr_t published=0, target=0, sharedBridge=0;
    std::array<uint8_t,10> patch{};
    bool operator==(const WriterSnapshot&) const = default;
};

inline bool DeltaReady(const Reader& r,uintptr_t base,Provider menu,bool forBoost,
                       bool (*executable)(void*,uintptr_t,size_t)=FlightCaveRegion) {
    std::array<uint8_t,kStockDelta.size()> entry{};
    if (!r.Get(base+kDeltaRva,entry)) return false;
    if (entry==kStockDelta) {
        std::array<uint8_t,kStockDelta.size()> again{};
        return r.Get(base+kDeltaRva,again) && again==entry;
    }

    const auto* writer=menu ? FlightWriterFor(menu.profile) : nullptr;
    if (!writer ||
        memcmp(entry.data(),kStockDelta.data(),8) || entry[8]!=0xE9 ||
        memcmp(entry.data()+13,"\x90\x90\x90\x90\x90",5) ||
        (forBoost && !NeutralFlight(r,menu,*writer))) return false;

    const auto site=base+kWriterRva;
    int32_t displacement=0;
    memcpy(&displacement,entry.data()+9,sizeof(displacement));
    const auto cave=static_cast<uintptr_t>(static_cast<int64_t>(site+5)+displacement);
    if (cave<0x10000 || cave>=0x800000000000ULL-kFlightCave.size() ||
        !executable(reinterpret_cast<void*>(base),cave,kFlightCave.size())) return false;

    const auto observe=[&](WriterSnapshot& proof) {
        return r.Get(menu.module+writer->published,proof.published) && proof.published==site &&
               r.Get(menu.module+writer->patch,proof.patch) &&
               !memcmp(proof.patch.data(),entry.data()+8,proof.patch.size()) &&
               r.Get(menu.module+writer->target,proof.target) && proof.target==site &&
               r.Get(menu.module+writer->sharedBridge,proof.sharedBridge) && !proof.sharedBridge;
    };
    auto expected=kFlightCave;
    for (size_t i=0;i<writer->slots.size();++i) {
        const auto address=menu.module+writer->slots[i];
        memcpy(expected.data()+kFlightCaveAddresses[i],&address,sizeof(address));
    }
    const auto continuation=site+10;
    memcpy(expected.data()+kFlightCaveAddresses.back(),&continuation,sizeof(continuation));
    WriterSnapshot first{}, second{};
    std::array<uint8_t,kFlightCave.size()> actual{};
    std::array<uint8_t,kStockDelta.size()> again{};
    return observe(first) && r.Get(cave,actual) && actual==expected &&
           r.Get(base+kDeltaRva,again) && again==entry &&
           observe(second) && first==second && r.Get(cave,actual) && actual==expected &&
           (!forBoost || NeutralFlight(r,menu,*writer));
}

#include "AdaptiveMenuContract.h"

inline Provider IdentifyRetainedMenu(const Reader& reader, HMODULE owner) {
    const auto module=reinterpret_cast<uintptr_t>(owner);
    if (!module) return {};
    const Profile* known=nullptr;
    for (const auto* candidate : {&kEarlier,&kUpdated,&kMenu601,&kMenu604}) {
        if (MovementSlots(reader,{module,candidate})) {
            if (known) return {1,nullptr};
            known=candidate;
        }
    }
    if (known) return {module,known};

    // Only the worker resolves a provider. This record is immutable once
    // published, so all physics reads remain bounded to its verified slots.
    static Profile retained{};
    static AdaptiveLayout layout{};
    static uintptr_t retainedModule=0;
    if (retainedModule) return retainedModule==module ? Provider{module,&retained} : Provider{1,nullptr};
    try {
        if (adaptive::Discover(reader,module,retained,layout) &&
            MovementSlots(reader,{module,&retained})) {
            const auto& writer=layout.flight;
            const bool verifiedSlots=WritableSlot(module,module+writer.published,sizeof(uintptr_t)) &&
                WritableSlot(module,module+writer.patch,10) &&
                WritableSlot(module,module+writer.target,sizeof(uintptr_t)) &&
                WritableSlot(module,module+writer.sharedBridge,sizeof(uintptr_t)) &&
                std::all_of(writer.slots.begin(),writer.slots.end(),
                            [&](uintptr_t rva){return WritableSlot(module,module+rva,16);});
            if (verifiedSlots) {
                retainedModule=module;
                return {module,&retained};
            }
        }
    } catch (...) {
        // Preserve the stock path when a foreign behavior could not be resolved.
    }
    return {1,nullptr};
}

}
