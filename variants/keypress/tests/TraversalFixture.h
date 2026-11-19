#pragma once
#include "../source/SuperJumpTraversalGuard.h"
#include <map>
namespace guardfixture {
using namespace superjump;
struct Fixture {
 std::map<uintptr_t,uint8_t> bytes;uintptr_t changing=0;unsigned reads=0;bool tear=false;
 boss::Profile p{0x140000000,0,0,0,true,0x20000};
 static constexpr uintptr_t root=0x30000,context=0x40000,actor=0x50000,parts=0x60000,status=0x70000,type=0x80000,registry=0x90000,buckets=0xa0000,slots=0xb0000,node=0xc0000,controller=0xd0000,wrapper=0xe0000,movement=0xf0000,physics=0x100000,ground=0x110000,contact=0x120000,component=0x130000,body=0x1b0000;
 template<class T>void Put(uintptr_t a,T x){const auto*pbytes=reinterpret_cast<const uint8_t*>(&x);for(size_t i=0;i<sizeof(x);++i)bytes[a+i]=pbytes[i];}
 static bool Read(void*c,uintptr_t a,void*out,size_t n){auto&f=*static_cast<Fixture*>(c);auto*d=static_cast<uint8_t*>(out);for(size_t i=0;i<n;++i){auto it=f.bytes.find(a+i);if(it==f.bytes.end())return false;d[i]=it->second;}if(a==f.changing&&++f.reads==1&&f.tear)f.Put<uint8_t>(a,2);return true;}
 boss::Reader Reader(){return {this,Read};}
 Fixture(){
 p.root_slot=0x21000;Put(p.root_slot,uintptr_t(0x140000));Put(uintptr_t(0x140098),uintptr_t(0x150000));
 Put(root,uintptr_t(0x160000));Put<uint8_t>(0x160048,4);Put(uintptr_t(0x160030),uintptr_t(0x170000));Put<uint8_t>(0x170009,1);
 for(uintptr_t i=0;i<0x90;++i)Put<uint8_t>(0x150000+i,0);Put<uint8_t>(0x150030,4);Put<uint8_t>(0x150032,17);
 Put(parts+0x90,uintptr_t(0));

 Put(p.actor_root_slot,root);Put(root+0x30,context);Put(context+0x50,actor);Put(context+8,registry);Put(actor,p.base+0x5991108);Put<uint32_t>(actor+0x60,42);Put<uint8_t>(actor+0x5e,0x10);Put(actor+0x68,parts);Put(parts+0x20,status);Put(actor+0x88,type);Put<uint8_t>(type+1,1);Put<uint8_t>(status+0x293,0);Put<uint64_t>(status+0x3b8,0);Put<uint64_t>(status+0x3c0,0);
 Put<uint32_t>(registry+0x88,1);Put<uint32_t>(registry+0x8c,1);Put(registry+0x98,buckets);Put(registry+0xa0,slots);Put<uint32_t>(buckets,1);Put<uint32_t>(buckets+8,42);Put<uint32_t>(buckets+12,0);Put(slots,node);Put<uint32_t>(node+4,42);Put(node+8,actor);
 Put(parts+0x40,controller);Put(controller+0x58,wrapper);Put(wrapper,actor);Put(controller+0xc8,movement);Put(movement+0x68,actor);Put(movement+0x60,physics);Put(physics+0xd8,ground);Put(physics+0x140,contact);Put(contact+0x2d0,body);Ground(true);Put(parts+0x78,component);Put(component,p.base+0x5994CC8);Put(component+8,actor);Lanes(7,7);
 }
 void Ground(bool yes){Put<uint8_t>(ground+0x1098,yes?1:0);Put<int32_t>(ground+0x1078,yes?0:-1);Put<uint8_t>(ground+0x1099,1);Put<uint8_t>(contact+0x468,0);Put<uint8_t>(contact+0x2fe,1);}
 void Lanes(uint8_t a,uint8_t b){Put(component+0xa4c,a);Put(component+0xa4c+0x1a0,b);}
 traversal::Snapshot Sample(){return traversal::Sample(Reader(),p,true);}
};
}
