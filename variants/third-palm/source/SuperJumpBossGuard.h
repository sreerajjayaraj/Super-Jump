#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace superjump::boss {
enum class State { Unknown, Inactive, Active };
enum class Reason { Profile, Root, Player, Registry, Lists, Opponent, Info, Changed, Ineligible, NoBoss, Boss };
struct Result {
    State state=State::Unknown;
    Reason reason=Reason::Profile;
    uintptr_t player=0;
    uint32_t player_uid=0;
    uint32_t opponents=0;
    uint32_t boss_uid=0;
    uintptr_t actual_vtable=0,context=0,world_root=0,combat=0;
    unsigned trace_stage=0;
    bool legacy_engaged=false;
    uint8_t selector=0,direct_flag=0,action_flag=0;
    uint32_t local_count=0,info_seen=0,missing_uids=0,first_candidate_uid=0;
    uint16_t first_info_index=0;uint8_t first_boss_flag=0;
    std::array<uint32_t,3> engagement_counts{},list_counts{};
};
inline bool BlocksBoost(const Result& r) { return r.state!=State::Inactive; }
using ReadFn=bool(*)(void*,uintptr_t,void*,size_t);
struct Reader {
    void* context=nullptr;
    ReadFn read=nullptr;
    bool Bytes(uintptr_t at,void* out,size_t n) const {
        constexpr uintptr_t maximum=0x800000000000ULL;
        return read && out && at>=0x10000 && n && at<maximum && n<=maximum-at && read(context,at,out,n);
    }
    template<class T> bool Get(uintptr_t at,T& out) const { return Bytes(at,&out,sizeof(out)); }
};
struct Profile {
    uintptr_t base=0,root_slot=0,info_slot=0,player_vtable=0;
    bool valid=false;
    uintptr_t actor_root_slot=0;
};
inline constexpr uint32_t kMaxBuckets=1U<<20;
inline constexpr uint32_t kMaxSlots=1U<<20;
inline constexpr uint32_t kMaxOpponents=4096;
inline bool NativeActorType(uint8_t v) { return v==1 || v==3 || v==4 || v==5 || v==6 || v==8 || v==9; }
// Exact known native actor families share the UID/parts/status layout. The
// general player getter returns this typed base family, not only UserActor.
// ChildOnly has the same zero-offset ClientActor/CommonActor bases and exact
// acquisition/UID/188 methods as Normal; immutable RTTI and methods are pinned.
inline bool KnownPrimary(const Profile& p,uintptr_t vtable) {
    return vtable==p.player_vtable || vtable==p.base+0x5991108 ||
        vtable==p.base+0x5991630 || vtable==p.base+0x5990BF0 || vtable==p.base+0x5BEB4B8;
}
// ActorInfo IDs now address a 24-byte native entry. A missing object or
// changing table must leave the encounter unknown.
struct InfoEntry {
    uintptr_t pool=0, entries=0, info=0;
    uint32_t count=0;
    bool operator==(const InfoEntry&) const = default;
};
inline bool ReadInfoEntry(const Reader& r,const Profile& p,uint16_t index,InfoEntry& out) {
    return r.Get(p.info_slot,out.pool) && r.Get(out.pool+0x28,out.count) &&
           out.count<=65536 && index<out.count &&
           r.Get(out.pool+0x20,out.entries) && out.entries>=0x10000 &&
           r.Get(out.entries+static_cast<uintptr_t>(index)*24+8,out.info) &&
           out.info>=0x10000;
}

struct Engagement {
    uint8_t selector=0,action_flag=0,direct_flag=0;
    uintptr_t action_component=0,action_state=0;
    uint32_t local_count=0;
    std::array<uint32_t,3> enemy_counts{};
    bool active=false;
};
inline bool SameEngagement(const Engagement& a,const Engagement& b) {
    return a.selector==b.selector && a.action_flag==b.action_flag && a.direct_flag==b.direct_flag &&
        a.action_component==b.action_component && a.action_state==b.action_state &&
        a.local_count==b.local_count && a.enemy_counts==b.enemy_counts && a.active==b.active;
}
inline bool ReadEngagement(const Reader& r,const Profile& p,uintptr_t vtable,
                           uintptr_t player,uintptr_t parts,uintptr_t combat,Engagement& e) {
    // 23ECE10 calls188(5). UserActor/CommonActor returnfalse; the three general
    // ClientActor families implement5AE580, which returns actor+C8!=0.
    const bool general=vtable==p.base+0x5991108 || vtable==p.base+0x5991630 || vtable==p.base+0x5990BF0;
    if(general && !r.Get(player+0xC8,e.selector))return false;
    if(!e.selector) {
        if(!r.Get(combat+0x164,e.direct_flag))return false;
        e.active=e.direct_flag!=0;return true;
    }
    if(!r.Get(parts+0x58,e.action_component))return false;
    if(e.action_component) {
        if(!r.Get(e.action_component+0x50,e.action_state) || !r.Get(e.action_state+0x40,e.action_flag))return false;
        if(e.action_flag==3)return true;
    }
    if(!r.Get(combat+0x128,e.local_count) || e.local_count>kMaxOpponents)return false;
    for(size_t i=0;i<3;++i)
        if(!r.Get(combat+0xA0+i*0x10,e.enemy_counts[i]) || e.enemy_counts[i]>kMaxOpponents)return false;
    e.active=e.local_count!=0 || e.enemy_counts[0]!=0 || e.enemy_counts[1]!=0 || e.enemy_counts[2]!=0;
    return true;
}
struct Registry {
    uintptr_t pointer=0,buckets=0,slots=0;
    uint32_t bucket_count=0,entries=0;
};
inline bool LoadRegistry(const Reader& r,uintptr_t pointer,Registry& out) {
    out.pointer=pointer;
    return r.Get(pointer+0x88,out.bucket_count) && r.Get(pointer+0x8C,out.entries) &&
        r.Get(pointer+0x98,out.buckets) && r.Get(pointer+0xA0,out.slots) &&
        out.bucket_count>0 && out.bucket_count<=kMaxBuckets && out.entries>0 && out.entries<=kMaxSlots &&
        out.buckets>=0x10000 && out.slots>=0x10000;
}
inline bool SameRegistry(const Reader& r,const Registry& before) {
    Registry after{};
    return LoadRegistry(r,before.pointer,after) && before.bucket_count==after.bucket_count &&
        before.entries==after.entries && before.buckets==after.buckets && before.slots==after.slots;
}
enum class Lookup { Bad, Missing, Found };
inline Lookup FindActor(const Reader& r,const Registry& map,uint32_t uid,uintptr_t& actor) {
    actor=0;
    // Native 996850 rejects zero and the special non-actor UID family.
    if(uid==0 || (uid&0x30000000U)==0x10000000U) return Lookup::Missing;
    const uintptr_t bucket=map.buckets+static_cast<uintptr_t>(uid%map.bucket_count)*0x100;
    uint32_t count=0;
    if(!r.Get(bucket,count) || count>31) return Lookup::Bad;
    for(uint32_t i=0;i<count;++i) {
        uint32_t key=0,index=0;
        if(!r.Get(bucket+8+i*8,key) || !r.Get(bucket+12+i*8,index)) return Lookup::Bad;
        if(key!=uid) continue;
        if(index>=kMaxSlots) return Lookup::Bad;
        uintptr_t node=0,value=0;uint32_t node_key=0;
        if(!r.Get(map.slots+static_cast<uintptr_t>(index)*8,node) || !r.Get(node+4,node_key)) return Lookup::Bad;
        if(node_key!=uid) continue; // Same collision handling as native 509160.
        if(!r.Get(node+8,value)) return Lookup::Bad;
        if(value<0x10000) return Lookup::Missing;
        uint32_t actual=0;uint8_t flags=0;
        if(!r.Get(value+0x60,actual) || !r.Get(value+0x5E,flags)) return Lookup::Bad;
        // A removed/recycled actor must not resurrect a retained boss UID.
        if(actual!=uid || !(flags&0x10)) return Lookup::Missing;
        uintptr_t again=0;uint32_t again_key=0;
        if(!r.Get(map.slots+static_cast<uintptr_t>(index)*8,again) || again!=node ||
           !r.Get(node+4,again_key) || again_key!=uid || !r.Get(node+8,again) || again!=value) return Lookup::Bad;
        actor=value;return Lookup::Found;
    }
    return Lookup::Missing;
}
#include "SuperJumpActiveActor.h"

struct List { uintptr_t data=0;uint32_t count=0; };
inline Result Sample(const Reader& r,const Profile& p) {
    Result out{};
    if(!p.valid) return out;
    uintptr_t root=0,context=0,player=0,registry=0,vtable=0,parts=0,status=0,combat=0,type=0;
    if(!r.Get(p.actor_root_slot,root) || !r.Get(root+0x30,context) || !(player=ReadActiveActor(r,p,context))) {out.reason=Reason::Root;return out;}
    out.player=player;out.world_root=root;
    out.context=context;
    if(!r.Get(player,vtable)) {out.reason=Reason::Player;return out;}
    out.actual_vtable=vtable;
    // Capture UID separately so an unsupported class cannot hide its identity
    // behind a default zero diagnostic produced by short-circuit evaluation.
    if(!r.Get(player+0x60,out.player_uid) || !KnownPrimary(p,vtable) ||
       !r.Get(context+8,registry) || !r.Get(player+0x68,parts) || !r.Get(parts+0x20,status) ||
       !r.Get(player+0x88,type)) {out.reason=Reason::Player;return out;}
    Registry map{};
    if(!LoadRegistry(r,registry,map)) {out.reason=Reason::Registry;return out;}
    uintptr_t self=0;
    if(FindActor(r,map,out.player_uid,self)!=Lookup::Found || self!=player) {out.reason=Reason::Player;return out;}
    uint8_t actor_type=0,dead=0;uint64_t flags_a=0,flags_b=0;
    if(!r.Get(type+1,actor_type) || !r.Get(status+0x293,dead) ||
       !r.Get(status+0x3B8,flags_a) || !r.Get(status+0x3C0,flags_b)) {out.reason=Reason::Player;return out;}
    bool combat_seen=false,engaged_seen=false;Engagement engaged{};
    // Final coherence is required even for a known inactive result.
    auto finish=[&](State state,Reason reason)->Result {
        uintptr_t q=0;uint32_t id=0;uint8_t byte=0;uint64_t flag=0;
        bool stable=MatchesActiveActor(r,p,root,context,player) && r.Get(context+8,q)&&q==registry &&
            r.Get(player,q)&&q==vtable && r.Get(player+0x60,id)&&id==out.player_uid &&
            r.Get(player+0x68,q)&&q==parts && r.Get(parts+0x20,q)&&q==status &&
            r.Get(player+0x88,q)&&q==type && r.Get(type+1,byte)&&byte==actor_type &&
            r.Get(status+0x293,byte)&&byte==dead && r.Get(status+0x3B8,flag)&&flag==flags_a &&
            r.Get(status+0x3C0,flag)&&flag==flags_b && SameRegistry(r,map);
        if(stable && combat_seen) stable=r.Get(parts+0x90,q)&&q==combat;
        if(stable && engaged_seen) {
            Engagement current{};
            stable=r.Get(combat+8,q)&&q==player && ReadEngagement(r,p,vtable,player,parts,combat,current) && SameEngagement(engaged,current);
        }
        if(stable) {uintptr_t found=0;stable=FindActor(r,map,out.player_uid,found)==Lookup::Found&&found==player;}
        out.state=stable?state:State::Unknown;out.reason=stable?reason:Reason::Changed;
        return out;
    };
    // Exact 19290C0 eligibility, with unsigned 64-bit flag comparison.
    if(!NativeActorType(actor_type) || dead || (flags_a|flags_b)>=0x80ULL)
        return finish(State::Inactive,Reason::Ineligible);
    if(!r.Get(parts+0x90,combat)) return finish(State::Unknown,Reason::Player);
    combat_seen=true;out.combat=combat;out.trace_stage=1;
    if(combat==0) return finish(State::Inactive,Reason::NoBoss);
    uintptr_t owner=0;
    if(!r.Get(combat+8,owner)||owner!=player || !ReadEngagement(r,p,vtable,player,parts,combat,engaged)) return finish(State::Unknown,Reason::Player);
    engaged_seen=true;out.trace_stage=2;out.legacy_engaged=engaged.active;
    out.selector=engaged.selector;out.direct_flag=engaged.direct_flag;out.action_flag=engaged.action_flag;
    out.local_count=engaged.local_count;out.engagement_counts=engaged.enemy_counts;
    if(!engaged.active) return finish(State::Inactive,Reason::NoBoss);
    // Both23ECE10 branches are replayed by memory reads only. No engine calls,
    // TLS-dependent reference acquisition or virtual calls run on this worker.
    out.trace_stage=3;
    std::array<List,3> lists{};
    uint32_t total=0;
    for(size_t i=0;i<lists.size();++i) {
        if(!r.Get(combat+0x98+i*0x10,lists[i].data) || !r.Get(combat+0xA0+i*0x10,lists[i].count) ||
           lists[i].count>kMaxOpponents-total || (lists[i].count && lists[i].data<0x10000))
            return finish(State::Unknown,Reason::Lists);
        total+=lists[i].count;out.list_counts[i]=lists[i].count;
    }
    std::array<uint32_t,kMaxOpponents> uid_snapshot{};
    size_t snapshot_count=0;
    for(const auto& list:lists) for(uint32_t i=0;i<list.count;++i)
        if(!r.Get(list.data+static_cast<uintptr_t>(i)*8,uid_snapshot[snapshot_count++]))
            return finish(State::Unknown,Reason::Lists);
    auto finish_lists=[&](State state,Reason reason)->Result {
        uintptr_t q=0;uint32_t n=0;Engagement current{};
        if(!r.Get(parts+0x90,q)||q!=combat || !ReadEngagement(r,p,vtable,player,parts,combat,current)||!SameEngagement(current,engaged))
            return finish(State::Unknown,Reason::Changed);
        for(size_t i=0;i<lists.size();++i)
            if(!r.Get(combat+0x98+i*0x10,q)||q!=lists[i].data || !r.Get(combat+0xA0+i*0x10,n)||n!=lists[i].count)
                return finish(State::Unknown,Reason::Changed);
        size_t index=0;
        for(const auto& list:lists) for(uint32_t i=0;i<list.count;++i) {
            uint32_t uid=0;
            if(!r.Get(list.data+static_cast<uintptr_t>(i)*8,uid)||uid!=uid_snapshot[index++])
                return finish(State::Unknown,Reason::Changed);
        }
        return finish(state,reason);
    };
    bool active=false;
    for(const auto& list:lists) for(uint32_t i=0;i<list.count;++i) {
        uint32_t uid=0;
        if(!r.Get(list.data+static_cast<uintptr_t>(i)*8,uid)) return finish_lists(State::Unknown,Reason::Lists);
        ++out.opponents;
        if(uid==out.player_uid) continue;
        uintptr_t actor=0;
        const auto lookup=FindActor(r,map,uid,actor);
        if(lookup==Lookup::Bad) return finish_lists(State::Unknown,Reason::Opponent);
        if(lookup==Lookup::Missing){++out.missing_uids;continue;}
        uintptr_t actor_parts=0,actor_status=0;
        InfoEntry info{};
        uint16_t info_index=0;uint8_t boss=0;
        if(!r.Get(actor+0x68,actor_parts) || !r.Get(actor_parts+0x20,actor_status) ||
           !r.Get(actor_status+0x30,info_index) || !ReadInfoEntry(r,p,info_index,info) || !r.Get(info.info+0x26,boss))
            return finish_lists(State::Unknown,Reason::Info);
        ++out.info_seen;
        if(!out.first_candidate_uid){out.first_candidate_uid=uid;out.first_info_index=info_index;out.first_boss_flag=boss;}
        if(boss!=1) continue;
        // Native CheckBossBattle applies eligibility only to the main actor.
        // A defeated boss retained in an engaged enemy list remains a boss
        // encounter until the native main/list/UID state clears it.
        InfoEntry again_info{};
        uintptr_t again=0;uint32_t again_uid=0;uint16_t again_index=0;uint8_t again_boss=0;
        if(FindActor(r,map,uid,again)!=Lookup::Found || again!=actor || !r.Get(actor+0x60,again_uid)||again_uid!=uid ||
           !r.Get(actor+0x68,again)||again!=actor_parts || !r.Get(actor_parts+0x20,again)||again!=actor_status ||
           !r.Get(actor_status+0x30,again_index)||again_index!=info_index ||
           !ReadInfoEntry(r,p,again_index,again_info)||!(again_info==info) ||
           !r.Get(info.info+0x26,again_boss)||again_boss!=1)
            return finish_lists(State::Unknown,Reason::Changed);
        active=true;out.boss_uid=uid;
    }
    return finish_lists(active?State::Active:State::Inactive,active?Reason::Boss:Reason::NoBoss);
}
} // namespace superjump::boss
