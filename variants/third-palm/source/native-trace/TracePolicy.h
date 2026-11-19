#pragma once
#include "../SuperJumpBossGuard.h"
namespace motiontrace {
using Reader=superjump::boss::Reader;using Profile=superjump::boss::Profile;
enum class Hero { Unknown,Kliff,Damiane,Oongka };
enum class Reason { Profile,Root,Identity,Registry,Hero,Controller,Slot,Ground,Commit,Changed,Valid };
struct Slot {
 uintptr_t pointer=0,first=0,second=0,metadata=0,active=0;
 uint32_t skill_key=0,name_hash=0,raw54=0;uint8_t metadata_valid=0,character_state=0;
 bool present=false,effective=false,suppressed=false;
 bool operator==(const Slot&)const=default;
};
struct Snapshot {
 bool valid=false,ground_known=false,grounded=false;Hero hero=Hero::Unknown;Reason reason=Reason::Profile;
 uintptr_t root=0,context=0,actor=0,vtable=0,parts=0,status=0,type=0,registry=0,buckets=0,registry_slots=0,pool=0,infos=0,info=0,controller=0,owner_wrapper=0,movement_state=0,physics=0,ground=0,contact=0;
 uint32_t sample_changed=0;Reason second_reason=Reason::Profile;
 uint32_t uid=0,hero_key=0,bucket_count=0,registry_entries=0,info_count=0,jump_velocity_bits=0,jump_height_bits=0,jump_param384_bits=0,jump_param388_bits=0;
 uint16_t info_index=0;uint8_t actor_type=0,dead=0,jump_request=0,jump_blocked=0,ground_f90=0,ground_f91=0,contact_458=0,contact_2ee=0;int32_t ground_f70=0;uint64_t flags_a=0,flags_b=0;
 Slot upper{},lower{};
 bool operator==(const Snapshot&)const=default;
};
inline Hero ClassifyHero(uint32_t key){return key==1?Hero::Kliff:key==4?Hero::Damiane:key==6?Hero::Oongka:Hero::Unknown;}
inline bool FiniteBits(uint32_t x){return (x&0x7F800000U)!=0x7F800000U;}
inline bool ReadSlot(const Reader&r,uintptr_t pointer,Slot&x){
 x.pointer=pointer;if(!pointer)return true;
 if(!r.Get(pointer,x.first)||!r.Get(pointer+8,x.second)||!r.Get(pointer+0x10,x.metadata)||!r.Get(pointer+0x118,x.active))return false;
 if(!x.first||!x.second||!x.metadata)return true;
 if(!r.Get(x.metadata+0x111,x.metadata_valid))return false;if(!x.metadata_valid)return true;
 if(!r.Get(x.metadata+0xf0,x.character_state)||!r.Get(x.metadata+0x18,x.name_hash)||!r.Get(x.metadata+0xa8,x.skill_key)||!r.Get(x.metadata+0x54,x.raw54))return false;
 x.present=true;x.effective=x.active!=0;return true;
}
inline Snapshot ReadOnce(const Reader&r,const Profile&p,uintptr_t eventState){
 Snapshot x{};if(!p.valid)return x;
 x.reason=Reason::Root;
 if(!r.Get(p.actor_root_slot,x.root)||!r.Get(x.root+0x30,x.context)||!(x.actor=superjump::boss::ReadActiveActor(r,p,x.context))||!r.Get(x.context+8,x.registry))return x;
 x.reason=Reason::Identity;
 if(!r.Get(x.actor,x.vtable)||!superjump::boss::KnownPrimary(p,x.vtable)||!r.Get(x.actor+0x60,x.uid)||!r.Get(x.actor+0x68,x.parts)||!r.Get(x.parts+0x20,x.status)||!r.Get(x.actor+0x88,x.type)||!r.Get(x.type+1,x.actor_type)||!superjump::boss::NativeActorType(x.actor_type))return x;
 // This grounding contract exists only on the three current Client families.
 if(x.vtable!=p.base+0x5991108&&x.vtable!=p.base+0x5990BF0&&x.vtable!=p.base+0x5991630)return x;
 if(!r.Get(x.status+0x293,x.dead)||!r.Get(x.status+0x3b8,x.flags_a)||!r.Get(x.status+0x3c0,x.flags_b)||x.dead||(x.flags_a|x.flags_b)>=0x80ULL)return x;
 x.reason=Reason::Registry;superjump::boss::Registry map{};uintptr_t found=0;
 if(!superjump::boss::LoadRegistry(r,x.registry,map)||superjump::boss::FindActor(r,map,x.uid,found)!=superjump::boss::Lookup::Found||found!=x.actor)return x;
 x.buckets=map.buckets;x.registry_slots=map.slots;x.bucket_count=map.bucket_count;x.registry_entries=map.entries;
 x.reason=Reason::Hero;
 superjump::boss::InfoEntry info{};
 if(!r.Get(x.status+0x30,x.info_index)||
    !superjump::boss::ReadInfoEntry(r,p,x.info_index,info)||
    !r.Get(info.info+8,x.hero_key))return x;
 x.pool=info.pool;x.infos=info.entries;x.info_count=info.count;x.info=info.info;
 x.hero=ClassifyHero(x.hero_key);if(x.hero==Hero::Unknown)return x;
 x.reason=Reason::Controller;uintptr_t owner=0,stateOwner=0;
 if(!r.Get(x.parts+0x40,x.controller)||!r.Get(x.controller+0x58,x.owner_wrapper)||!r.Get(x.owner_wrapper,owner)||owner!=x.actor||!r.Get(x.controller+0xc8,x.movement_state)||!r.Get(x.movement_state+0x68,stateOwner)||stateOwner!=x.actor||(eventState&&eventState!=x.movement_state))return x;
 x.reason=Reason::Slot;uintptr_t up=0,low=0;
 if(!r.Get(x.controller+0x88,up)||!r.Get(x.controller+0x90,low)||!ReadSlot(r,up,x.upper)||!ReadSlot(r,low,x.lower))return x;
 // Native checks upper validity/state independently of its active pointer.
 if(x.upper.present&&x.upper.character_state==2){x.lower.effective=false;x.lower.suppressed=true;}
 x.reason=Reason::Ground;
 if(!r.Get(x.movement_state+0x60,x.physics)||!r.Get(x.physics+0xd8,x.ground)||!r.Get(x.physics+0x140,x.contact)||!r.Get(x.ground+0x1098,x.ground_f90)||!r.Get(x.ground+0x1078,x.ground_f70)||!r.Get(x.ground+0x1099,x.ground_f91)||!r.Get(x.contact+0x468,x.contact_458)||!r.Get(x.contact+0x2fe,x.contact_2ee))return x;
 x.ground_known=true;x.grounded=(x.ground_f90&&x.ground_f70!=-1)||(!x.ground_f91&&(x.contact_458==1||!x.contact_2ee));
 x.reason=Reason::Commit;
 if(!r.Get(x.movement_state+0x380,x.jump_request)||!r.Get(x.movement_state+0x3ad,x.jump_blocked)||!r.Get(x.movement_state+0x3a0,x.jump_velocity_bits)||!r.Get(x.movement_state+0x38c,x.jump_height_bits)||!r.Get(x.movement_state+0x384,x.jump_param384_bits)||!r.Get(x.movement_state+0x388,x.jump_param388_bits))return x;
 if(!FiniteBits(x.jump_velocity_bits)||!FiniteBits(x.jump_height_bits)||!FiniteBits(x.jump_param384_bits)||!FiniteBits(x.jump_param388_bits))return x;
 if(eventState&&(x.jump_request||x.jump_height_bits||x.jump_blocked))return x;
 if(!superjump::boss::MatchesActiveActor(r,p,x.root,x.context,x.actor)||!superjump::boss::SameRegistry(r,map)||superjump::boss::FindActor(r,map,x.uid,found)!=superjump::boss::Lookup::Found||found!=x.actor){x.reason=Reason::Changed;return x;}
 x.valid=true;x.reason=Reason::Valid;return x;
}
inline uint32_t ChangedFields(const Snapshot&a,const Snapshot&b){
 uint32_t mask=0;
 if(a.root!=b.root||a.context!=b.context||a.actor!=b.actor||a.vtable!=b.vtable||a.parts!=b.parts||a.status!=b.status||a.type!=b.type||a.uid!=b.uid||a.dead!=b.dead||a.flags_a!=b.flags_a||a.flags_b!=b.flags_b)mask|=1;
 if(a.registry!=b.registry||a.buckets!=b.buckets||a.registry_slots!=b.registry_slots||a.bucket_count!=b.bucket_count||a.registry_entries!=b.registry_entries)mask|=2;
 if(a.hero!=b.hero||a.hero_key!=b.hero_key||a.info!=b.info||a.info_index!=b.info_index||a.pool!=b.pool||a.infos!=b.infos||a.info_count!=b.info_count)mask|=4;
 if(a.controller!=b.controller||a.owner_wrapper!=b.owner_wrapper||a.movement_state!=b.movement_state)mask|=8;
 if(a.upper!=b.upper||a.lower!=b.lower)mask|=16;
 if(a.ground_known!=b.ground_known||a.grounded!=b.grounded||a.physics!=b.physics||a.ground!=b.ground||a.contact!=b.contact||a.ground_f90!=b.ground_f90||a.ground_f91!=b.ground_f91||a.ground_f70!=b.ground_f70||a.contact_458!=b.contact_458||a.contact_2ee!=b.contact_2ee)mask|=32;
 if(a.jump_request!=b.jump_request||a.jump_blocked!=b.jump_blocked||a.jump_velocity_bits!=b.jump_velocity_bits||a.jump_height_bits!=b.jump_height_bits||a.jump_param384_bits!=b.jump_param384_bits||a.jump_param388_bits!=b.jump_param388_bits)mask|=64;
 if(a.valid!=b.valid||a.reason!=b.reason)mask|=128;
 return mask;
}
inline Snapshot Sample(const Reader&r,const Profile&p,uintptr_t eventState=0){
 auto first=ReadOnce(r,p,eventState);if(!first.valid)return first;
 auto second=ReadOnce(r,p,eventState);if(!second.valid||!(first==second)){first.sample_changed=ChangedFields(first,second);first.second_reason=second.reason;first.valid=false;first.reason=Reason::Changed;}return first;
}
}
