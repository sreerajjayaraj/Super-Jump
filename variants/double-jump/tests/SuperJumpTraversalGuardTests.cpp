#include "../source/SuperJumpTraversalGuard.h"
#include <cstdio>
#include <map>
#include <vector>
using namespace superjump;
int checks=0,failures=0;void Check(bool x,const char*n){++checks;if(!x){++failures;std::printf("FAIL %s\n",n);}}
struct Fixture {
 std::map<uintptr_t,uint8_t> bytes;uintptr_t changing=0;unsigned reads=0;bool tear=false;
 boss::Profile p{0x140000000,0,0,0,true,0x20000};
 static constexpr uintptr_t root=0x30000,context=0x40000,actor=0x50000,parts=0x60000,status=0x70000,type=0x80000,registry=0x90000,buckets=0xa0000,slots=0xb0000,node=0xc0000,controller=0xd0000,wrapper=0xe0000,movement=0xf0000,physics=0x100000,ground=0x110000,contact=0x120000,component=0x130000,body=0x1b0000;
 template<class T>void Put(uintptr_t a,T x){const auto*pbytes=reinterpret_cast<const uint8_t*>(&x);for(size_t i=0;i<sizeof(x);++i)bytes[a+i]=pbytes[i];}
 static bool Read(void*c,uintptr_t a,void*out,size_t n){auto&f=*static_cast<Fixture*>(c);auto*d=static_cast<uint8_t*>(out);for(size_t i=0;i<n;++i){auto it=f.bytes.find(a+i);if(it==f.bytes.end())return false;d[i]=it->second;}if(a==f.changing&&++f.reads==1&&f.tear)f.Put<uint8_t>(a,2);return true;}
 boss::Reader Reader(){return {this,Read};}
 Fixture(){
 Put(p.actor_root_slot,root);Put(root+0x30,context);Put(context+0x50,actor);Put(context+8,registry);Put(actor,p.base+0x5991108);Put<uint32_t>(actor+0x60,42);Put<uint8_t>(actor+0x5e,0x10);Put(actor+0x68,parts);Put(parts+0x20,status);Put(actor+0x88,type);Put<uint8_t>(type+1,1);Put<uint8_t>(status+0x293,0);Put<uint64_t>(status+0x3b8,0);Put<uint64_t>(status+0x3c0,0);
 Put<uint32_t>(registry+0x88,1);Put<uint32_t>(registry+0x8c,1);Put(registry+0x98,buckets);Put(registry+0xa0,slots);Put<uint32_t>(buckets,1);Put<uint32_t>(buckets+8,42);Put<uint32_t>(buckets+12,0);Put(slots,node);Put<uint32_t>(node+4,42);Put(node+8,actor);
 Put(parts+0x40,controller);Put(controller+0x58,wrapper);Put(wrapper,actor);Put(controller+0xc8,movement);Put(movement+0x68,actor);Put(movement+0x60,physics);Put(physics+0xd8,ground);Put(physics+0x140,contact);Put(contact+0x2d0,body);Ground(true);Put(parts+0x78,component);Put(component,p.base+0x5994CC8);Put(component+8,actor);Lanes(7,7);
 }
 void Ground(bool yes){Put<uint8_t>(ground+0x1098,yes?1:0);Put<int32_t>(ground+0x1078,yes?0:-1);Put<uint8_t>(ground+0x1099,1);Put<uint8_t>(contact+0x468,0);Put<uint8_t>(contact+0x2fe,1);}
 void Lanes(uint8_t a,uint8_t b){Put(component+0xa4c,a);Put(component+0xa4c+0x1a0,b);}
 traversal::Snapshot Sample(){return traversal::Sample(Reader(),p,true);}
};
int main(){
 Fixture f;auto x=f.Sample();Check(x.valid&&x.grounded&&x.lane0==7&&x.lane1==7,"owned current actor idle grounded sample");traversal::Epoch e{};
 f.Ground(false);Check(!traversal::Allows(e,f.Sample(),true),"airborne startup cannot guess normal jump origin");
 f.Ground(true);Check(traversal::Allows(e,f.Sample(),true),"verified grounded reset establishes eligible epoch");f.Ground(false);Check(traversal::Allows(e,f.Sample(),true),"ordinary airborne jump retains eligibility");
 for(uint8_t lane=0;lane<2;++lane)for(uint8_t state=0;state<7;++state){f.Ground(true);f.Lanes(7,7);Check(traversal::Allows(e,f.Sample(),true),"grounded rearm");f.Ground(false);f.Lanes(lane?7:state,lane?state:7);Check(!traversal::Allows(e,f.Sample(),true),"either native non-reset lane blocks independent of timer/input");f.Lanes(7,7);Check(!traversal::Allows(e,f.Sample(),true),"release/reset while airborne keeps traversal epoch excluded");}
 f.Ground(true);Check(traversal::Allows(e,f.Sample(),true),"landing clears traversal exclusion");f.Ground(false);Check(!traversal::Allows(e,f.Sample(),false),"context loss invalidates epoch");Check(!traversal::Allows(e,f.Sample(),true),"menu/focus regain airborne cannot rearm");
 f.Ground(true);Check(traversal::Allows(e,f.Sample(),true),"landed context restores eligibility");f.Ground(false);auto changed=f.Sample();++changed.uid;Check(!traversal::Allows(e,changed,true),"UID recycling invalidates eligible epoch");
 f.Ground(true);Check(traversal::Allows(e,f.Sample(),true),"restore original grounded actor");f.Ground(false);auto bad=f.Sample();bad.valid=false;Check(!traversal::Allows(e,bad,true),"unknown sample invalidates epoch");Check(!traversal::Allows(e,f.Sample(),true),"transient unknown does not restore old airborne epoch");
 for(unsigned field=0;field<5;++field){Fixture q;if(field==0)q.Put(q.component+8,q.actor+8);if(field==1)q.Put(q.component,q.p.base+0x5994CC0);if(field==2)q.Put(q.wrapper,q.actor+8);if(field==3)q.Put(q.movement+0x68,q.actor+8);if(field==4)q.Put<uint32_t>(q.node+4,43);Check(!q.Sample().valid,"foreign/stale ownership rejects sample");}
 Fixture torn;torn.changing=torn.component+0xa4c;torn.tear=true;Check(!torn.Sample().valid,"changed native lane fails double coherence");Fixture unread;unread.bytes.erase(unread.contact+0x2fe);Check(!unread.Sample().valid,"unreadable ground source fails closed");
 Fixture ground;ground.Put<uint8_t>(ground.ground+0x1098,0);ground.Put<uint8_t>(ground.ground+0x1099,0);ground.Put<uint8_t>(ground.contact+0x468,1);Check(ground.Sample().grounded,"native contact branch grounds");ground.Put<uint8_t>(ground.contact+0x468,0);ground.Put<uint8_t>(ground.contact+0x2fe,0);Check(ground.Sample().grounded,"native secondary contact grounds");ground.Put<uint8_t>(ground.ground+0x1099,1);Check(!ground.Sample().grounded,"native F91 suppresses contact branch");
 Check(!traversal::Sample(f.Reader(),f.p,false).valid,"unvalidated exact native profile blocks");
 // Exact-game roster fixture. The older default actor stays present in the
 // context while a separate native owner and movement body are selected.
 Fixture active;
 active.p.actor_root_slot=active.p.base+0x72451C0;
 active.p.info_slot=0x200000;
 active.Put(active.p.actor_root_slot,active.root);
 constexpr uintptr_t roster=0x210000, info=0x220000, rows=0x230000;
 active.Put(active.context+0x148,roster);
 active.Put(active.context+0x150,uint32_t{3});
 active.Put(active.context+0x154,uint32_t{3});
 active.Put(active.p.info_slot,info);
 active.Put(info+0x20,rows);
 active.Put(info+0x28,uint32_t{6});
 active.Put(active.registry+0x8c,uint32_t{3});
 active.Put(active.buckets,uint32_t{3});
 const std::array<uint32_t,3> uids{42,0xB0100002,0xB0100003};
 const std::array<uint16_t,3> definitions{0,3,5};
 const auto add=[&](size_t index){
     const uintptr_t shift=index*0x300000, actor=active.actor+shift;
     const auto copy=active.bytes;
     if(index) for(const auto& [address,value]:copy)
         if(address>=active.actor && address<=active.body && (address<active.registry || address>=active.controller))
             active.Put(address+shift,value);
     active.Put(roster+8*index,actor);
     active.Put(rows+24*definitions[index]+8,rows+0x100+index*0x40);
     active.Put(rows+0x108+index*0x40,uint32_t{1U+definitions[index]});
     active.Put(active.buckets+8+8*index,uids[index]);
     active.Put(active.buckets+12+8*index,uint32_t(index));
     active.Put(active.slots+index*8,active.node+index*0x40);
     active.Put(active.node+index*0x40+4,uids[index]);
     active.Put(active.node+index*0x40+8,actor);
     active.Put(actor+0x60,uids[index]);
     active.Put(actor+0x68,active.parts+shift);
     active.Put(actor+0x88,active.type+shift);
     active.Put(actor+0xA0,active.context);
     active.Put(actor+0x94,uint8_t{0});
     active.Put(active.type+shift+1,uint8_t(index?4:1));
     active.Put(active.parts+shift+0x20,active.status+shift);
     active.Put(active.status+shift+0x30,definitions[index]);
     active.Put(active.parts+shift+0x40,active.controller+shift);
     active.Put(active.controller+shift+0x58,active.wrapper+shift);
     active.Put(active.wrapper+shift,actor);
     active.Put(active.controller+shift+0xC8,active.movement+shift);
     active.Put(active.movement+shift+0x68,actor);
     active.Put(active.movement+shift+0x60,active.physics+shift);
     active.Put(active.physics+shift+0xD8,active.ground+shift);
     active.Put(active.physics+shift+0x140,active.contact+shift);
     active.Put(active.contact+shift+0x2D0,active.body+shift);
     active.Put(active.parts+shift+0x78,active.component+shift);
     active.Put(active.component+shift+8,actor);
 };
 for(size_t i=0;i<3;++i)add(i);
 active.Put(active.context+0x58,active.context);
 const auto select=[&](size_t index) {
     for(size_t i=0;i<3;++i)active.Put(active.actor+i*0x300000+0x94,uint8_t(i==index));
 };
 for(size_t i:{0U,1U,2U,0U}) {
     select(i);
     const auto selected=active.Sample();
     Check(selected.valid && selected.uid==uids[i] &&
           selected.frame_body==active.body+i*0x300000,
           "traversal follows the switched actor and its own frame body");
     const auto boss=superjump::boss::Sample(active.Reader(),active.p);
     Check(boss.player_uid==uids[i], "boss guard uses the same controlled actor");
 }
 select(3);
 Check(!active.Sample().valid,"no controlled actor blocks the boost during a switch");
 active.Put(active.actor+0x94,uint8_t{1});
 active.Put(active.actor+0x300000+0x94,uint8_t{1});
 Check(!active.Sample().valid,"ambiguous local control cannot arm a jump");
 select(1);
 Check(active.Sample().uid==uids[1],"native work list discovers Damiane");
 active.Put(active.context+0x150,uint32_t{0});
 Check(active.Sample().valid && active.Sample().uid==uids[1] &&
       superjump::boss::Sample(active.Reader(),active.p).player_uid==uids[1],
       "Damiane remains bound when the native frame work list empties");
 select(2);
 Check(!active.Sample().valid,"former owner cannot survive a real loss of local control");
 active.Put(active.context+0x150,uint32_t{3});
 Check(active.Sample().valid && active.Sample().uid==uids[2],"native work list discovers Oongka");
 active.Put(active.context+0x150,uint32_t{0});
 Check(active.Sample().valid && active.Sample().uid==uids[2],
       "Oongka remains bound between native actor updates");
 active.Put(active.node+2*0x40+4,uint32_t{0});
 Check(!active.Sample().valid,"retained actor requires the same canonical native registry entry");
 std::printf("traversal production policy: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
