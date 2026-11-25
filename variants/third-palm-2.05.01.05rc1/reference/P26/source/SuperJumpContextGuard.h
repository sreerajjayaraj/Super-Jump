#pragma once
#include "SuperJumpBossGuard.h"
namespace superjump::context {
enum class UiState { Unknown, Gameplay, Prohibited };
enum class UiReason { Profile, Root, Mode, Flags, Pending, NonGameplay, Changed, WorldState, Gameplay };
struct UiResult {
    UiState state=UiState::Unknown;
    UiReason reason=UiReason::Profile;
    uintptr_t root=0,mode_object=0;
    uint8_t mode=0xFF,submode=0xFF,dirty=0xFF;
    uint32_t queued=0,mode_requests=0,subtype_requests=0;
    uintptr_t world_root=0,world_stack=0,world_entry=0;
    uint8_t world_state=0xFF,gameplay_enabled=0;
};
struct ModeSnapshot {
    uint8_t mode=0,submode=0,dirty=0;
    std::array<uint8_t,7> modes{};
    std::array<uint8_t,18> subtypes{};
    uint32_t queued=0;
};
inline bool ReadMode(const boss::Reader& r,uintptr_t at,ModeSnapshot& s) {
    return r.Get(at+0x30,s.mode) && r.Get(at+0x32,s.submode) && r.Get(at+0x66,s.dirty) &&
        r.Get(at+0x80,s.queued) && r.Bytes(at+0x3A,s.modes.data(),s.modes.size()) &&
        r.Bytes(at+0x41,s.subtypes.data(),s.subtypes.size());
}
inline bool SameMode(const ModeSnapshot& a,const ModeSnapshot& b) {
    return a.mode==b.mode && a.submode==b.submode && a.dirty==b.dirty && a.queued==b.queued &&
        a.modes==b.modes && a.subtypes==b.subtypes;
}
inline UiResult SampleUi(const boss::Reader& r,const boss::Profile& p) {
    UiResult out{};if(!p.valid)return out;
    if(!r.Get(p.root_slot,out.root) || !r.Get(out.root+0x98,out.mode_object)) {out.reason=UiReason::Root;return out;}
    if(!r.Get(p.actor_root_slot,out.world_root) || !r.Get(out.world_root,out.world_stack) ||
       !r.Get(out.world_stack+0x48,out.world_state) || out.world_state>=7 ||
       !r.Get(out.world_stack+0x10+static_cast<uintptr_t>(out.world_state)*8,out.world_entry) ||
       !r.Get(out.world_entry+9,out.gameplay_enabled)) {out.reason=UiReason::WorldState;return out;}
    ModeSnapshot first{};
    if(!ReadMode(r,out.mode_object,first)) {out.reason=UiReason::Mode;return out;}
    out.mode=first.mode;out.submode=first.submode;out.dirty=first.dirty;out.queued=first.queued;
    if(first.mode>7 || first.submode>18 || first.dirty>1 || first.queued>4096) {out.reason=UiReason::Flags;return out;}
    bool other_request=false;
    for(size_t i=0;i<first.modes.size();++i){if(first.modes[i]>1){out.reason=UiReason::Flags;return out;}if(first.modes[i])out.mode_requests|=1U<<i;if(i!=4 && first.modes[i])other_request=true;}
    for(size_t i=0;i<first.subtypes.size();++i){if(first.subtypes[i]>1){out.reason=UiReason::Flags;return out;}if(first.subtypes[i])out.subtype_requests|=1U<<i;if(i!=17 && first.subtypes[i])other_request=true;}
    // Native7052B2..70556C uses66 only to refresh optional cached mode31 /
    // subtype flags53..64. It can remain1 while committed30/32 are4/17.
    // Selection705167..7051B4 and commit705785..7059C4 are independent.
    if(first.queued || other_request)out.reason=UiReason::Pending;
    else if(!out.gameplay_enabled){out.state=UiState::Prohibited;out.reason=UiReason::WorldState;}
    else if(first.mode!=4 || first.submode!=17){out.state=UiState::Prohibited;out.reason=UiReason::NonGameplay;}
    else {out.state=UiState::Gameplay;out.reason=UiReason::Gameplay;}
    uintptr_t root=0,mode=0;ModeSnapshot again{};
    if(!r.Get(p.root_slot,root)||root!=out.root || !r.Get(root+0x98,mode)||mode!=out.mode_object ||
       !ReadMode(r,mode,again)||!SameMode(first,again)) {out.state=UiState::Unknown;out.reason=UiReason::Changed;}
    uintptr_t world=0,stack=0,entry=0;uint8_t state=0,enabled=0;
    if(!r.Get(p.actor_root_slot,world)||world!=out.world_root || !r.Get(world,stack)||stack!=out.world_stack ||
       !r.Get(stack+0x48,state)||state!=out.world_state ||
       !r.Get(stack+0x10+static_cast<uintptr_t>(state)*8,entry)||entry!=out.world_entry ||
       !r.Get(entry+9,enabled)||enabled!=out.gameplay_enabled){out.state=UiState::Unknown;out.reason=UiReason::Changed;}
    return out;
}
inline bool BlocksBoost(const UiResult& ui,const boss::Result& encounter,bool own_foreground,bool visible_os_cursor) {
    (void)visible_os_cursor; // OS visibility alone is not a native modal predicate.
    return !own_foreground || ui.state!=UiState::Gameplay || boss::BlocksBoost(encounter) ||
        !ui.world_root || ui.world_root!=encounter.world_root;
}
} // namespace superjump::context
