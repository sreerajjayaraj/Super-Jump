#pragma once

#include "native-trace/TraceNativeProfile.h"
#include "native-trace/RuntimeSupport.h"
#include "../shared/MenuFlight.h"

namespace superjump::palm::menu_mode {
using Reader=motiontrace::Reader;
inline constexpr uintptr_t kModeRva=0x731B40;
struct Profile {
    const menu60::Profile* movement;
    uintptr_t target_slot, original_slot, handler_rva, handler_row_rva;
    std::array<uint32_t,3> handler_row;
    std::array<uint8_t,51> handler;
    const char* name;
};
#include "PalmMenuModeProfiles.generated.h"
struct Provider {
    uintptr_t module=0;
    const Profile* profile=nullptr;
    explicit operator bool() const { return module>1 && profile; }
    menu60::Provider Movement() const { return {module,profile ? profile->movement : nullptr}; }
};
inline constexpr size_t kStolen=12,kBridgeSize=kStolen+14;
inline constexpr size_t kTypedStolen=5,kTypedRelay=kTypedStolen+14,kTypedBridgeSize=kTypedRelay+14;
struct TypedProfile {
    uintptr_t target_slot, original_slot, handler_rva, handler_row_rva;
    std::array<uint32_t,3> handler_row;
    std::array<uint8_t,26> handler;
};
inline constexpr TypedProfile kUpdatedTypedMode{
    0x6A1B00, 0x6A1D48, 0x106340, 0x82F2A8,
    {0x106340,0x1063FC,0x5F1A04},
    {0x48,0x89,0x5C,0x24,0x10,0x57,0x48,0x83,0xEC,0x40,0x48,0x8B,0xF9,
     0x48,0x8B,0x05,0xF4,0xB9,0x59,0x00,0xFF,0x15,0x0E,0x26,0x2C,0x00}
};
#include "PalmMenu601Profiles.generated.h"
#include "PalmMenu604Profiles.generated.h"

inline Profile kAdaptiveMode{};
inline TypedProfile kAdaptiveTypedMode{};

inline const TypedProfile* TypedModeFor(const Profile* profile) {
    if (profile==&kUpdatedMode) return &kUpdatedTypedMode;
    if (profile==&kMenu601Mode) return &kMenu601TypedMode;
    if (profile==&kMenu604Mode) return &kMenu604TypedMode;
    if (profile==&kAdaptiveMode && kAdaptiveTypedMode.handler_rva) return &kAdaptiveTypedMode;
    return nullptr;
}
inline constexpr std::array<uint8_t,kStolen> kStockMode{0x48,0x89,0x5C,0x24,0x18,0x55,0x56,0x57,0x41,0x54,0x41,0x55};
static_assert(motiontrace::native::kBlocks[16].rva==kModeRva &&
              motiontrace::native::kBlocks[16].size>=kStolen);
inline const Profile* ModeForMovement(const menu60::Profile* movement) {
    if (movement==&menu60::kEarlier) return &kEarlierMode;
    if (movement==&menu60::kUpdated) return &kUpdatedMode;
    if (movement==&menu60::kMenu601) return &kMenu601Mode;
    if (movement==&menu60::kMenu604) return &kMenu604Mode;
    return nullptr;
}
inline const Profile* BindAdaptiveMode(const Reader& reader,menu60::Provider movement) {
    const auto& layout=movement.profile->adaptive->mode;
    const auto owner=movement.module;
    const auto& legacy=layout.legacy;
    const auto& typed=layout.typed;
    // A missing optional ModeSwitch pattern still permits untouched game code.
    // View refuses an actual write whose typed/legacy ownership is unproven.
    const auto unchanged=[&](uintptr_t rva,const auto& expected) {
        auto actual=expected;
        return !rva || (reader.Get(owner+rva,actual) && actual==expected);
    };
    if (!unchanged(legacy.entry,layout.legacyBytes) || !unchanged(typed.entry,layout.typedBytes))
        return nullptr;
    kAdaptiveMode={movement.profile,legacy.target,legacy.original,legacy.entry,legacy.rowRva,
                   legacy.row,layout.legacyBytes,"verified Menu ModeSwitch"};
    kAdaptiveTypedMode={typed.target,typed.original,typed.entry,typed.rowRva,typed.row,layout.typedBytes};
    return &kAdaptiveMode;
}

inline Provider RetainExactMenu(const Reader& r) {
    HMODULE owner=nullptr;
    if (!GetModuleHandleExW(0,L"DesertLinkModMenu.asi",&owner))
        return GetLastError()==ERROR_MOD_NOT_FOUND ? Provider{} : Provider{1,nullptr};
    const auto module=reinterpret_cast<uintptr_t>(owner);
    const auto movement=menu60::IdentifyRetainedMenu(r,owner);
    const auto* profile=!movement ? nullptr : movement.profile->adaptive ?
        BindAdaptiveMode(r,movement) : ModeForMovement(movement.profile);
    const auto* typed=TypedModeFor(profile);
    const bool typedRegions=!typed ||
        (menu60::WritableSlot(module,module+typed->target_slot,sizeof(uintptr_t)) &&
         menu60::WritableSlot(module,module+typed->original_slot,sizeof(uintptr_t)) &&
         native_memory::Region(module+typed->handler_rva,typed->handler.size(),module,MEM_IMAGE,PAGE_EXECUTE_READ) &&
         native_memory::Region(module+typed->handler_row_rva,sizeof(typed->handler_row),module,MEM_IMAGE,PAGE_READONLY));
    const bool legacyRegions=profile && (!profile->handler_rva ||
        (menu60::WritableSlot(module,module+profile->target_slot,sizeof(uintptr_t)) &&
         menu60::WritableSlot(module,module+profile->original_slot,sizeof(uintptr_t)) &&
         native_memory::Region(module+profile->handler_rva,profile->handler.size(),module,MEM_IMAGE,PAGE_EXECUTE_READ) &&
         native_memory::Region(module+profile->handler_row_rva,sizeof(profile->handler_row),module,MEM_IMAGE,PAGE_READONLY)));
    if (legacyRegions && typedRegions) return {module,profile};
    FreeLibrary(owner);
    return {1,nullptr};
}
struct Snapshot {
    uintptr_t target=0, original=0;
    std::array<uint8_t,kStolen> entry{};
    std::array<uint8_t,kBridgeSize> bridge{};
    bool operator==(const Snapshot&) const = default;
};
struct TypedSnapshot {
    uintptr_t target=0, original=0;
    std::array<uint8_t,kTypedStolen> entry{};
    std::array<uint8_t,kTypedBridgeSize> bridge{};
    bool operator==(const TypedSnapshot&) const = default;
};
class View {
    Reader raw_{};
    uintptr_t entry_=0;
    Provider menu_{};
    motiontrace::ExecFn private_exec_=nullptr;
    void* exec_context_=nullptr;
    bool normalized_=false;

    bool Observe(Snapshot& s) const {
        const auto& p=*menu_.profile;
        decltype(Profile::handler) handler{};
        decltype(Profile::handler_row) row{};
        const auto module=menu_.module;
        if (!p.handler_rva || !raw_.Get(module+p.target_slot,s.target) || s.target!=entry_ ||
            !raw_.Get(module+p.original_slot,s.original) ||
            !raw_.Get(module+p.handler_row_rva,row) || row!=p.handler_row ||
            !raw_.Get(module+p.handler_rva,handler) || handler!=p.handler ||
            !raw_.Get(entry_,s.entry) || s.entry[0]!=0x48 || s.entry[1]!=0xB8 ||
            s.entry[10]!=0xFF || s.entry[11]!=0xE0 ||
            s.original<0x10000 || s.original>=0x800000000000ULL-kBridgeSize ||
            !private_exec_(exec_context_,s.original,kBridgeSize) ||
            !raw_.Get(s.original,s.bridge)) return false;
        uintptr_t destination=0;
        memcpy(&destination,s.entry.data()+2,sizeof(destination));
        return destination==module+p.handler_rva &&
               !memcmp(s.bridge.data(),kStockMode.data(),kStolen) &&
               menu60::JumpMatches(s.bridge.data()+kStolen,entry_+kStolen);
    }

    bool ObserveTyped(TypedSnapshot& s) const {
        const auto module=menu_.module;
        const auto* typed=TypedModeFor(menu_.profile);
        decltype(TypedProfile::handler) handler{};
        decltype(TypedProfile::handler_row) row{};
        if (!typed ||
            !raw_.Get(module+typed->target_slot,s.target) || s.target!=entry_ ||
            !raw_.Get(module+typed->original_slot,s.original) ||
            !raw_.Get(module+typed->handler_row_rva,row) || row!=typed->handler_row ||
            !raw_.Get(module+typed->handler_rva,handler) || handler!=typed->handler ||
            !raw_.Get(entry_,s.entry) || s.entry[0]!=0xE9 ||
            s.original<0x10000 || s.original>=0x800000000000ULL-kTypedBridgeSize ||
            !private_exec_(exec_context_,s.original,kTypedBridgeSize) ||
            !raw_.Get(s.original,s.bridge)) return false;
        int32_t displacement=0;
        memcpy(&displacement,s.entry.data()+1,sizeof(displacement));
        const auto relay=static_cast<int64_t>(entry_+kTypedStolen)+displacement;
        return relay==static_cast<int64_t>(s.original+kTypedRelay) &&
               !memcmp(s.bridge.data(),kStockMode.data(),kTypedStolen) &&
               menu60::JumpMatches(s.bridge.data()+kTypedStolen,entry_+kTypedStolen) &&
               menu60::JumpMatches(s.bridge.data()+kTypedRelay,module+typed->handler_rva);
    }

public:
    View(Reader raw,uintptr_t base,Provider menu,
         motiontrace::ExecFn exec=motiontrace::runtime::PrivateExecutable,void* context=nullptr)
        :raw_(raw),entry_(base+kModeRva),menu_(menu),private_exec_(exec),exec_context_(context){}
    static bool Read(void* opaque,uintptr_t at,void* out,size_t size) {
        auto& view=*static_cast<View*>(opaque);
        if (!view.raw_.Bytes(at,out,size)) return false;
        if (at!=view.entry_ || size<kStolen ||
            !memcmp(out,kStockMode.data(),kStolen)) return true;
        if (!view.menu_ || !view.private_exec_) return false;
        size_t stolen=kStolen;
        if (static_cast<const uint8_t*>(out)[0]==0xE9) {
            TypedSnapshot a{},b{};
            if (!view.ObserveTyped(a) || !view.ObserveTyped(b) || a!=b ||
                memcmp(out,a.entry.data(),kTypedStolen)) return false;
            stolen=kTypedStolen;
        } else {
            Snapshot a{},b{};
            if (!view.Observe(a) || !view.Observe(b) || a!=b ||
                memcmp(out,a.entry.data(),kStolen)) return false;
        }
        memcpy(out,kStockMode.data(),stolen);
        view.normalized_=true;
        return true;
    }
    Reader Wrapped() { return {this,Read}; }
    bool Normalized() const { return normalized_; }
};
}
