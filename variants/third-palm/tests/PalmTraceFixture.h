#pragma once
#include "../source/native-trace/TracePolicy.h"
#include <cstdio>
#include <functional>
#include <vector>
using namespace motiontrace;
struct Memory {
 std::vector<unsigned char> bytes=std::vector<unsigned char>(0x90000);uintptr_t next=0x10000;size_t reads=0,fail=0;std::function<void(uintptr_t)> mutation;
 uintptr_t Alloc(size_t n){auto p=next;next+=(n+15)&~size_t(15);return p;}
 template<class T>void Put(uintptr_t p,T v){std::memcpy(bytes.data()+p-0x10000,&v,sizeof(v));}
 static bool Read(void*c,uintptr_t p,void*out,size_t n){auto&m=*static_cast<Memory*>(c);++m.reads;if(m.reads==m.fail)return false;if(m.mutation)m.mutation(p);if(p<0x10000||p-0x10000>m.bytes.size()||n>m.bytes.size()-(p-0x10000))return false;std::memcpy(out,m.bytes.data()+p-0x10000,n);return true;}
};
struct Fixture {
 Memory m;superjump::boss::Profile p{};uintptr_t root,context,registry,buckets,slots,node,actor,parts,status,type,pool,infos,info,controller,owner,state,physics,ground,contact,upper,lower,umeta,lmeta;
 Fixture(){
  p.base=0x140000000;p.player_vtable=p.base+0x59C1D90;p.valid=true;p.actor_root_slot=m.Alloc(8);p.info_slot=m.Alloc(8);
  root=m.Alloc(0x50);context=m.Alloc(0x60);registry=m.Alloc(0xb0);buckets=m.Alloc(0x100);slots=m.Alloc(8);node=m.Alloc(16);
  actor=m.Alloc(0x100);parts=m.Alloc(0x200);status=m.Alloc(0x400);type=m.Alloc(16);pool=m.Alloc(0x60);infos=m.Alloc(48);info=m.Alloc(0x100);
  controller=m.Alloc(0xd0);owner=m.Alloc(16);state=m.Alloc(0x800);physics=m.Alloc(0x200);ground=m.Alloc(0x1100);contact=m.Alloc(0x600);upper=m.Alloc(0x130);lower=m.Alloc(0x130);umeta=m.Alloc(0x120);lmeta=m.Alloc(0x120);
  m.Put(p.actor_root_slot,root);m.Put(root+0x30,context);m.Put(context+0x50,actor);m.Put(context+8,registry);
  m.Put(registry+0x88,uint32_t(1));m.Put(registry+0x8c,uint32_t(1));m.Put(registry+0x98,buckets);m.Put(registry+0xa0,slots);m.Put(buckets,uint32_t(1));m.Put(buckets+8,uint32_t(1));m.Put(buckets+12,uint32_t(0));m.Put(slots,node);m.Put(node+4,uint32_t(1));m.Put(node+8,actor);
  m.Put(actor,p.base+0x5990BF0);m.Put(actor+0x5e,uint8_t(0x10));m.Put(actor+0x60,uint32_t(1));m.Put(actor+0x68,parts);m.Put(actor+0x88,type);m.Put(type+1,uint8_t(1));m.Put(parts+0x20,status);m.Put(status+0x30,uint16_t(1));
  m.Put(p.info_slot,pool);m.Put(pool+0x28,uint32_t(2));m.Put(pool+0x20,infos);m.Put(infos+32,info);m.Put(info+8,uint32_t(1));
  m.Put(parts+0x40,controller);m.Put(controller+0x58,owner);m.Put(owner,actor);m.Put(controller+0xc8,state);m.Put(state+0x68,actor);m.Put(state+0x60,physics);m.Put(physics+0xd8,ground);m.Put(physics+0x140,contact);m.Put(ground+0x1078,int32_t(-1));m.Put(contact+0x468,uint8_t(1));m.Put(contact+0x2fe,uint8_t(1));
  m.Put(controller+0x88,upper);m.Put(controller+0x90,lower);InitSlot(upper,umeta,15035,0x1690D50E,2);InitSlot(lower,lmeta,15033,123,0);
  m.Put(state+0x3a0,uint32_t(0x41200000));
 }
 void InitSlot(uintptr_t s,uintptr_t x,uint32_t skill,uint32_t hash,uint8_t f0){m.Put(s,uintptr_t(0x123400));m.Put(s+8,uintptr_t(0x123500));m.Put(s+0x10,x);m.Put(s+0x118,uintptr_t(0x123600));m.Put(x+0x111,uint8_t(1));m.Put(x+0xf0,f0);m.Put(x+0x18,hash);m.Put(x+0xa8,skill);}
 Snapshot Read(uintptr_t nativeState=0){m.reads=0;return Sample(superjump::boss::Reader{&m,Memory::Read},p,nativeState);}
};
