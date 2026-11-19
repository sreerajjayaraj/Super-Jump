#include "../source/SuperJumpBossGuard.h"
#include <cstdio>
#include <functional>
#include <vector>
using namespace superjump::boss;
struct Memory {
    std::vector<unsigned char> bytes=std::vector<unsigned char>(0x40000);
    std::function<void(uintptr_t)> mutation;
    uintptr_t next=0x10000;
    uintptr_t Alloc(size_t size) {auto p=next;next+=(size+15)&~size_t(15);return p;}
    template<class T> void Put(uintptr_t p,T v) {std::memcpy(bytes.data()+p-0x10000,&v,sizeof(v));}
    static bool Read(void* ctx,uintptr_t p,void* out,size_t n) {
        auto& m=*static_cast<Memory*>(ctx);
        if(m.mutation)m.mutation(p);
        if(p<0x10000 || p-0x10000>m.bytes.size() || n>m.bytes.size()-(p-0x10000))return false;
        std::memcpy(out,m.bytes.data()+p-0x10000,n);return true;
    }
};
struct Fixture {
    Memory m;
    Profile p{};
    uintptr_t root,context,map,buckets,slots,pool,infos,list,player,enemy,boss;
    uintptr_t ps,es,bs,pc,pt,et,bt;
    unsigned entries=0;
    Fixture() {
        p.valid=true;p.base=0x140000000;p.player_vtable=p.base+0x59C1D90;
        p.root_slot=m.Alloc(8);p.actor_root_slot=p.root_slot;p.info_slot=m.Alloc(8);root=m.Alloc(0x40);context=m.Alloc(0x60);
        map=m.Alloc(0xB0);buckets=m.Alloc(0x100);slots=m.Alloc(64);pool=m.Alloc(0x60);infos=m.Alloc(192);list=m.Alloc(64);
        m.Put(p.root_slot,root);m.Put(root,context);m.Put(root+0x30,context);m.Put(context+8,map);
        m.Put(map+0x88,uint32_t(1));m.Put(map+0x98,buckets);m.Put(map+0xA0,slots);
        m.Put(p.info_slot,pool);m.Put(pool+0x28,uint32_t(8));m.Put(pool+0x20,infos);
        player=Actor(1,false,ps,pt);enemy=Actor(2,false,es,et);boss=Actor(3,true,bs,bt);
        m.Put(context+0x50,player);m.Put(player,p.player_vtable);
        uintptr_t parts=0;Memory::Read(&m,player+0x68,&parts,8);pc=m.Alloc(0x180);m.Put(parts+0x90,pc);
        m.Put(pc+8,player);m.Put(pc+0x164,uint8_t(1));m.Put(pc+0x98,list);m.Put(pc+0xA0,uint32_t(1));m.Put(list,uint32_t(3));
    }
    uintptr_t Actor(uint32_t uid,bool isboss,uintptr_t& status,uintptr_t& type) {
        auto a=m.Alloc(0x100),parts=m.Alloc(0xA0);status=m.Alloc(0x400);type=m.Alloc(16);
        auto node=m.Alloc(16),info=m.Alloc(0x40);
        m.Put(a+0x5E,uint8_t(0x10));m.Put(a+0x60,uid);m.Put(a+0x68,parts);m.Put(a+0x88,type);
        m.Put(type+1,uint8_t(1));m.Put(parts+0x20,status);m.Put(status+0x30,uint16_t(uid));
        m.Put(infos+uid*24+8,info);m.Put(info+0x26,uint8_t(isboss?1:0));
        m.Put(node+4,uid);m.Put(node+8,a);m.Put(slots+entries*8,node);
        m.Put(buckets+8+entries*8,uid);m.Put(buckets+12+entries*8,uint32_t(entries));++entries;
        m.Put(buckets,uint32_t(entries));m.Put(map+0x8C,uint32_t(entries));return a;
    }
    Result SampleNow(){return Sample(Reader{&m,Memory::Read},p);}
};
template<class T> auto TraceStage(const T& r,int)->decltype(r.trace_stage){return r.trace_stage;}
template<class T> unsigned TraceStage(const T&,...){return 0;}
int main() {
    unsigned checks=0,failed=0;
    auto check=[&](bool ok,const char* name){++checks;if(!ok){++failed;std::printf("FAIL %s\n",name);}};
    auto test=[&](const char* name,State expected,auto mutate){Fixture f;mutate(f);check(f.SampleNow().state==expected,name);};
    {Fixture f;f.m.Put(f.player,f.p.base+0x5990BF0);f.m.Put(f.pc+0x164,uint8_t(0));constexpr uint32_t captured_uid=0xA0100001;uintptr_t node=0;Memory::Read(&f.m,f.slots,&node,8);f.m.Put(f.player+0x60,captured_uid);f.m.Put(node+4,captured_uid);f.m.Put(f.buckets+8,captured_uid);auto result=f.SampleNow();check(result.state==State::Inactive&&result.player_uid==captured_uid&&result.actual_vtable==f.p.base+0x5990BF0,"captured3460 ChildOnly A0100001 identity resolves and no encounter allows");}
    test("captured ChildOnly primary direct no encounter",State::Inactive,[](auto&f){f.m.Put(f.player,f.p.base+0x5990BF0);f.m.Put(f.pc+0x164,uint8_t(0));});
    test("ChildOnly primary direct boss",State::Active,[](auto&f){f.m.Put(f.player,f.p.base+0x5990BF0);});
    test("ChildOnly C8 engagement overrides direct flag",State::Active,[](auto&f){f.m.Put(f.player,f.p.base+0x5990BF0);f.m.Put(f.player+0xC8,uint8_t(1));f.m.Put(f.pc+0x164,uint8_t(0));});
    test("ChildOnly C8 no list ignores direct flag",State::Inactive,[](auto&f){f.m.Put(f.player,f.p.base+0x5990BF0);f.m.Put(f.player+0xC8,uint8_t(1));f.m.Put(f.pc+0xA0,uint32_t(0));});
    test("ChildOnly C8 ordinary opponent is not boss",State::Inactive,[](auto&f){f.m.Put(f.player,f.p.base+0x5990BF0);f.m.Put(f.player+0xC8,uint8_t(1));f.m.Put(f.list,uint32_t(2));});
    test("ChildOnly native C8 action3 suppresses engagement",State::Inactive,[](auto&f){f.m.Put(f.player,f.p.base+0x5990BF0);f.m.Put(f.player+0xC8,uint8_t(1));uintptr_t parts=0;Memory::Read(&f.m,f.player+0x68,&parts,8);auto component=f.m.Alloc(0x60),action=f.m.Alloc(0x50);f.m.Put(parts+0x58,component);f.m.Put(component+0x50,action);f.m.Put(action+0x40,uint8_t(3));});
    test("ChildOnly same native eligibility excludes type7",State::Inactive,[](auto&f){f.m.Put(f.player,f.p.base+0x5990BF0);f.m.Put(f.pt+1,uint8_t(7));});
    test("ChildOnly unsigned flags exclude subject",State::Inactive,[](auto&f){f.m.Put(f.player,f.p.base+0x5990BF0);f.m.Put(f.ps+0x3C0,uint64_t(0x8000000000000000));});
    {Fixture f;f.m.Put(f.player,f.p.base+0x5990BF0);f.m.Put(f.player+0xC8,uint8_t(1));unsigned reads=0;f.m.mutation=[&](uintptr_t address){if(address==f.player+0xC8&&++reads==2)f.m.Put(f.player+0xC8,uint8_t(0));};check(f.SampleNow().state==State::Unknown,"ChildOnly selector mutation stays unknown");}
    test("normal primary native direct inactive",State::Inactive,[](auto&f){f.m.Put(f.player,f.p.base+0x5991108);f.m.Put(f.pc+0x164,uint8_t(0));});
    test("normal primary native direct boss",State::Active,[](auto&f){f.m.Put(f.player,f.p.base+0x5991108);});
    test("generic primary normal combat",State::Inactive,[](auto&f){f.m.Put(f.player,f.p.base+0x5991630);f.m.Put(f.player+0xC8,uint8_t(1));f.m.Put(f.list,uint32_t(2));});
    test("normal primary C8 path boss ignores164",State::Active,[](auto&f){f.m.Put(f.player,f.p.base+0x5991108);f.m.Put(f.player+0xC8,uint8_t(1));f.m.Put(f.pc+0x164,uint8_t(0));});
    test("normal primary C8 action3 suppresses encounter",State::Inactive,[](auto&f){f.m.Put(f.player,f.p.base+0x5991108);f.m.Put(f.player+0xC8,uint8_t(1));uintptr_t parts=0;Memory::Read(&f.m,f.player+0x68,&parts,8);auto component=f.m.Alloc(0x60),action=f.m.Alloc(0x50);f.m.Put(parts+0x58,component);f.m.Put(component+0x50,action);f.m.Put(action+0x40,uint8_t(3));});
    test("common primary native direct branch",State::Active,[](auto&f){f.m.Put(f.player,f.p.base+0x5BEB4B8);f.m.Put(f.player+0xC8,uint8_t(1));});
    test("component owner mismatch",State::Unknown,[](auto&f){f.m.Put(f.pc+8,f.enemy);});
    test("eligible boss",State::Active,[](auto&){});
    test("ordinary combat",State::Inactive,[](auto&f){f.m.Put(f.list,uint32_t(2));});
    test("no combat",State::Inactive,[](auto&f){f.m.Put(f.pc+0x164,uint8_t(0));});
    test("combat flag2",State::Active,[](auto&f){f.m.Put(f.pc+0x164,uint8_t(2));});
    test("combat high bit",State::Active,[](auto&f){f.m.Put(f.pc+0x164,uint8_t(0x80));});
    test("multiple opponents",State::Active,[](auto&f){f.m.Put(f.pc+0xA0,uint32_t(3));f.m.Put(f.list,uint32_t(2));f.m.Put(f.list+8,uint32_t(3));f.m.Put(f.list+16,uint32_t(1));});
    test("boss before party",State::Active,[](auto&f){f.m.Put(f.pc+0xA0,uint32_t(2));f.m.Put(f.list+8,uint32_t(2));});
    test("third enemy list",State::Active,[](auto&f){f.m.Put(f.pc+0xA0,uint32_t(0));f.m.Put(f.pc+0xB8,f.list);f.m.Put(f.pc+0xC0,uint32_t(1));});
    test("self is not opponent",State::Inactive,[](auto&f){f.m.Put(f.list,uint32_t(1));});
    test("dead boss retained during active encounter",State::Active,[](auto&f){f.m.Put(f.bs+0x293,uint8_t(1));});
    test("dead boss after native combat clears",State::Inactive,[](auto&f){f.m.Put(f.bs+0x293,uint8_t(1));f.m.Put(f.pc+0x164,uint8_t(0));});
    test("removed boss",State::Inactive,[](auto&f){f.m.Put(f.list,uint32_t(4));});
    test("recycled boss",State::Inactive,[](auto&f){f.m.Put(f.boss+0x60,uint32_t(4));});
    test("invalid actor flag",State::Inactive,[](auto&f){f.m.Put(f.boss+0x5E,uint8_t(0));});
    test("candidate flags do not end native encounter",State::Active,[](auto&f){f.m.Put(f.bs+0x3B8,uint64_t(0x80));});
    test("candidate high flags do not end native encounter",State::Active,[](auto&f){f.m.Put(f.bs+0x3C0,uint64_t(0x8000000000000000));});
    test("main native exclusion",State::Inactive,[](auto&f){f.m.Put(f.ps+0x3B8,uint64_t(0x80));});
    test("main unsigned64 exclusion",State::Inactive,[](auto&f){f.m.Put(f.ps+0x3C0,uint64_t(0x8000000000000000));});
    test("main dead",State::Inactive,[](auto&f){f.m.Put(f.ps+0x293,uint8_t(1));});
    test("main type7",State::Inactive,[](auto&f){f.m.Put(f.pt+1,uint8_t(7));});
    test("candidate type7 does not change native boss classification",State::Active,[](auto&f){f.m.Put(f.bt+1,uint8_t(7));});
    test("uncached info",State::Unknown,[](auto&f){f.m.Put(f.infos+80,uintptr_t(0));});
    test("invalid pointer",State::Unknown,[](auto&f){f.m.Put(f.pc+0x98,uintptr_t(0x777777));});
    test("list count bound",State::Unknown,[](auto&f){f.m.Put(f.pc+0xA0,uint32_t(4097));});
    test("registry bucket bound",State::Unknown,[](auto&f){f.m.Put(f.buckets,uint32_t(32));});
    test("registry slot bound",State::Unknown,[](auto&f){f.m.Put(f.buckets+28,uint32_t(1<<20));});
    test("unknown profile",State::Unknown,[](auto&f){f.p.valid=false;});
    test("different main actor class",State::Unknown,[](auto&f){f.m.Put(f.player,uintptr_t(0x777777));});
    test("legal unsigned low flags",State::Active,[](auto&f){f.m.Put(f.ps+0x3B8,uint64_t(0x40));f.m.Put(f.bs+0x3C0,uint64_t(0x3F));});
    test("dead then live boss",State::Active,[](auto&f){f.m.Put(f.pc+0xA0,uint32_t(2));f.m.Put(f.list,uint32_t(2));f.m.Put(f.es+0x293,uint8_t(1));f.m.Put(f.list+8,uint32_t(3));});
    test("UID special family is ignored",State::Inactive,[](auto&f){f.m.Put(f.list,uint32_t(0x10000001));});
    test("zero UID is ignored",State::Inactive,[](auto&f){f.m.Put(f.list,uint32_t(0));});
    test("null combat component",State::Inactive,[](auto&f){uintptr_t parts=0;Memory::Read(&f.m,f.player+0x68,&parts,8);f.m.Put(parts+0x90,uintptr_t(0));});
    test("combined list limit",State::Unknown,[](auto&f){f.m.Put(f.pc+0xA0,uint32_t(4096));f.m.Put(f.pc+0xB0,uint32_t(1));});
    test("out of range info index",State::Unknown,[](auto&f){f.m.Put(f.bs+0x30,uint16_t(8));});
    test("noncanonical pointer",State::Unknown,[](auto&f){f.m.Put(f.pc+0x98,uintptr_t(0xFFFF800000000000));});
    for(uint8_t type: {uint8_t(1),uint8_t(3),uint8_t(4),uint8_t(5),uint8_t(6),uint8_t(8),uint8_t(9)}) {
        Fixture f;f.m.Put(f.pt+1,type);f.m.Put(f.bt+1,type);check(f.SampleNow().state==State::Active,"all native eligible actor types");
    }
    test("normal primary high C8 uses list branch",State::Active,[](auto&f){f.m.Put(f.player,f.p.base+0x5991108);f.m.Put(f.player+0xC8,uint8_t(0x80));f.m.Put(f.pc+0x164,uint8_t(0));});
    test("normal primary invalid action is unknown",State::Unknown,[](auto&f){f.m.Put(f.player,f.p.base+0x5991108);f.m.Put(f.player+0xC8,uint8_t(1));uintptr_t parts=0;Memory::Read(&f.m,f.player+0x68,&parts,8);auto component=f.m.Alloc(0x60);f.m.Put(parts+0x58,component);});
    test("normal primary no native lists is inactive",State::Inactive,[](auto&f){f.m.Put(f.player,f.p.base+0x5991108);f.m.Put(f.player+0xC8,uint8_t(1));f.m.Put(f.pc+0xA0,uint32_t(0));});
    test("normal primary alternate count bound",State::Unknown,[](auto&f){f.m.Put(f.player,f.p.base+0x5991108);f.m.Put(f.player+0xC8,uint8_t(1));f.m.Put(f.pc+0x128,uint32_t(4097));});
    {Fixture f;f.m.Put(f.player,uintptr_t(0x777777));auto sampled=f.SampleNow();check(sampled.state==State::Unknown&&sampled.player_uid==1&&sampled.actual_vtable==0x777777,"unsupported class diagnostics retain actual UID/vtable");}
    {Fixture f;f.m.Put(f.player,f.p.base+0x5991108);f.m.Put(f.player+0xC8,uint8_t(1));f.m.Put(f.pc+0xA0,uint32_t(0));f.m.Put(f.pc+0x128,uint32_t(1));uintptr_t parts=0;Memory::Read(&f.m,f.player+0x68,&parts,8);Engagement e{};check(ReadEngagement(Reader{&f.m,Memory::Read},f.p,f.p.base+0x5991108,f.player,parts,f.pc,e)&&e.active,"native alternate local128 count is engagement evidence");}
    {Fixture f;f.m.Put(f.player,f.p.base+0x5991108);f.m.Put(f.player+0xC8,uint8_t(1));unsigned reads=0;f.m.mutation=[&](uintptr_t at){if(at==f.player+0xC8&&++reads==2)f.m.Put(f.player+0xC8,uint8_t(0));};check(f.SampleNow().state==State::Unknown,"C8 selector mutation is unknown");}
    for(int mode=0;mode<7;++mode) {
        Fixture f;unsigned hits=0;uintptr_t boss_info=0;Memory::Read(&f.m,f.infos+80,&boss_info,8);
        f.m.mutation=[&](uintptr_t at){if(at==boss_info+0x26 && ++hits==2){
            if(mode==0)f.m.Put(f.list,uint32_t(2));
            if(mode==1)f.m.Put(f.context+0x50,f.enemy);
            if(mode==2)f.m.Put(f.ps+0x3B8,uint64_t(0x80));
            if(mode==3)f.m.Put(f.player+0x60,uint32_t(9));
            if(mode==4)f.m.Put(f.pc+8,f.enemy);
            if(mode==5)f.m.Put(f.map+0xA0,f.list);
            if(mode==6)f.m.Put(boss_info+0x26,uint8_t(0));
        }};
        check(f.SampleNow().state==State::Unknown,"identity/list/eligibility changes are unknown");
    }
    {Fixture f;uintptr_t parts=0;Memory::Read(&f.m,f.player+0x68,&parts,8);f.m.Put(parts+0x90,uintptr_t(0));auto r=f.SampleNow();check(r.state==State::Inactive&&TraceStage(r,0)==1,"trace distinguishes absent combat component without changing inactive result");}
    {Fixture f;f.m.Put(f.pc+0x164,uint8_t(0));auto r=f.SampleNow();check(r.state==State::Inactive&&TraceStage(r,0)==2,"trace distinguishes native engagement false without changing inactive result");}
    {Fixture f;f.m.Put(f.list,uint32_t(2));auto r=f.SampleNow();check(r.state==State::Inactive&&TraceStage(r,0)==3,"trace distinguishes scanned ordinary opponents without changing inactive result");}
    Fixture f;check(BlocksBoost(f.SampleNow()),"active blocks");f.m.Put(f.pc+0x164,uint8_t(0));
    check(!BlocksBoost(f.SampleNow()),"fresh inactive clears restriction");
    check(BlocksBoost(Result{}),"unknown blocks");
    std::printf("boss guard: %u checks, %u failures\n",checks,failed);return failed?1:0;
}
