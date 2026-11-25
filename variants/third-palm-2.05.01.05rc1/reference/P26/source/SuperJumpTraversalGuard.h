#pragma once
#include "SuperJumpBossGuard.h"
#include <tuple>
namespace superjump::traversal {
enum class Reason { Profile,Root,Identity,Registry,Controller,Ground,Component,Changed,Valid };
struct Snapshot {
 bool valid=false,grounded=false;Reason reason=Reason::Profile;
 uintptr_t root=0,context=0,actor=0,vtable=0,parts=0,status=0,type=0,registry=0,controller=0,wrapper=0,movement=0,physics=0,ground=0,contact=0,component=0,component_vtable=0,frame_body=0;
 uint32_t uid=0;uint64_t flags_a=0,flags_b=0;uint8_t actor_type=0,dead=0,f90=0,f91=0,c458=0,c2ee=0,lane0=0,lane1=0;int32_t f70=0;
 bool operator==(const Snapshot&o)const {
  return std::tie(valid,grounded,reason,root,context,actor,vtable,parts,status,type,registry,controller,wrapper,movement,physics,ground,contact,component,component_vtable,frame_body,uid,flags_a,flags_b,actor_type,dead,f90,f91,c458,c2ee,lane0,lane1,f70)==std::tie(o.valid,o.grounded,o.reason,o.root,o.context,o.actor,o.vtable,o.parts,o.status,o.type,o.registry,o.controller,o.wrapper,o.movement,o.physics,o.ground,o.contact,o.component,o.component_vtable,o.frame_body,o.uid,o.flags_a,o.flags_b,o.actor_type,o.dead,o.f90,o.f91,o.c458,o.c2ee,o.lane0,o.lane1,o.f70);
 }
};
inline Snapshot ReadOnce(const boss::Reader&r,const boss::Profile&p,bool native_valid){
 Snapshot x{};if(!p.valid||!native_valid)return x;x.reason=Reason::Root;
 if(!r.Get(p.actor_root_slot,x.root)||!r.Get(x.root+0x30,x.context)||!(x.actor=boss::ReadActiveActor(r,p,x.context))||!r.Get(x.context+8,x.registry))return x;
 x.reason=Reason::Identity;
 if(!r.Get(x.actor,x.vtable)||!boss::KnownGeneralActor(p,x.vtable)||!r.Get(x.actor+0x60,x.uid)||!r.Get(x.actor+0x68,x.parts)||!r.Get(x.parts+0x20,x.status)||!r.Get(x.actor+0x88,x.type)||!r.Get(x.type+1,x.actor_type)||!boss::NativeActorType(x.actor_type)||!r.Get(x.status+0x293,x.dead)||!r.Get(x.status+0x3b8,x.flags_a)||!r.Get(x.status+0x3c0,x.flags_b)||x.dead||(x.flags_a|x.flags_b)>=0x80ULL)return x;
 x.reason=Reason::Registry;boss::Registry map{};uintptr_t found=0;
 if(!boss::LoadRegistry(r,x.registry,map)||boss::FindActor(r,map,x.uid,found)!=boss::Lookup::Found||found!=x.actor)return x;
 x.reason=Reason::Controller;uintptr_t owner=0;
 if(!r.Get(x.parts+0x40,x.controller)||!r.Get(x.controller+0x58,x.wrapper)||!r.Get(x.wrapper,owner)||owner!=x.actor||!r.Get(x.controller+0xc8,x.movement)||!r.Get(x.movement+0x68,owner)||owner!=x.actor)return x;
 x.reason=Reason::Ground;
 if(!r.Get(x.movement+0x60,x.physics)||!r.Get(x.physics+0xd8,x.ground)||!r.Get(x.physics+0x140,x.contact)||!r.Get(x.ground+0x1098,x.f90)||!r.Get(x.ground+0x1078,x.f70)||!r.Get(x.ground+0x1099,x.f91)||!r.Get(x.contact+0x468,x.c458)||!r.Get(x.contact+0x2fe,x.c2ee))return x;
 if(!r.Get(x.contact+0x2d0,x.frame_body)||x.frame_body<0x10000||x.frame_body>=0x7FFFFFFFFF70ULL)return x;
 x.grounded=(x.f90&&x.f70!=-1)||(!x.f91&&(x.c458==1||!x.c2ee));
 x.reason=Reason::Component;
 if(!r.Get(x.parts+0x78,x.component)||!r.Get(x.component,x.component_vtable)||x.component_vtable!=p.base+(p.shifted_layout?0x5999DB0:0x5994CC8)||!r.Get(x.component+8,owner)||owner!=x.actor||!r.Get(x.component+0xa4c,x.lane0)||!r.Get(x.component+0xa4c+0x1a0,x.lane1))return x;
 if(!boss::MatchesActiveActor(r,p,x.root,x.context,x.actor)||!boss::SameRegistry(r,map)||boss::FindActor(r,map,x.uid,found)!=boss::Lookup::Found||found!=x.actor){x.reason=Reason::Changed;return x;}
 x.valid=true;x.reason=Reason::Valid;return x;
}
inline Snapshot Sample(const boss::Reader&r,const boss::Profile&p,bool native_valid){
 auto x=ReadOnce(r,p,native_valid);if(!x.valid)return x;const auto y=ReadOnce(r,p,native_valid);
 if(!y.valid||!(x==y)){x.valid=false;x.reason=Reason::Changed;}return x;
}
struct Epoch {
 uintptr_t root=0,context=0,actor=0,parts=0,component=0,controller=0,movement=0,physics=0,front=0,body=0;uint32_t uid=0;bool ready=false;
};
inline bool SameIdentity(const Epoch&e,const Snapshot&x){
 return e.root==x.root&&e.context==x.context&&e.actor==x.actor&&e.uid==x.uid&&e.parts==x.parts&&e.component==x.component&&e.controller==x.controller&&e.movement==x.movement&&e.physics==x.physics&&e.front==x.contact&&e.body==x.frame_body;
}
inline bool Allows(Epoch&e,const Snapshot&x,bool context_allowed){
 if(!context_allowed||!x.valid){e={};return false;}
 const bool same=SameIdentity(e,x);
 if(!same){e={x.root,x.context,x.actor,x.parts,x.component,x.controller,x.movement,x.physics,x.contact,x.frame_body,x.uid,false};}
 if(x.lane0!=7||x.lane1!=7){e.ready=false;return false;}
 if(x.grounded)e.ready=true;
 return e.ready;
}
}
