#include "../source/SuperJumpContextGuard.h"
#include <cstdio>
#include <functional>
#include <vector>
using namespace superjump;
struct Memory {
 std::vector<uint8_t> bytes=std::vector<uint8_t>(0x1000);
 std::function<void(uintptr_t)> mutate;
 template<class T>void Put(uintptr_t at,T value){std::memcpy(bytes.data()+at-0x10000,&value,sizeof(value));}
 static bool Read(void* ctx,uintptr_t at,void* out,size_t size){auto&m=*static_cast<Memory*>(ctx);if(m.mutate)m.mutate(at);if(at<0x10000 || at-0x10000>m.bytes.size() || size>m.bytes.size()-(at-0x10000))return false;memcpy(out,m.bytes.data()+at-0x10000,size);return true;}
};
struct Fixture {
 Memory m;boss::Profile p{};boss::Result encounter{};
 Fixture(){p.valid=true;p.root_slot=0x10000;p.actor_root_slot=0x10008;m.Put(0x10008,uintptr_t(0x10300));m.Put(0x10300,uintptr_t(0x10400));m.Put(0x10448,uint8_t(4));m.Put(0x10430,uintptr_t(0x10500));m.Put(0x10509,uint8_t(1));encounter.world_root=0x10300;m.Put(0x10000,uintptr_t(0x10100));m.Put(0x10198,uintptr_t(0x10200));m.Put(0x10230,uint8_t(4));m.Put(0x10232,uint8_t(17));m.Put(0x1023E,uint8_t(1));m.Put(0x10252,uint8_t(1));encounter.state=boss::State::Inactive;}
 context::UiResult Ui(){return context::SampleUi(boss::Reader{&m,Memory::Read},p);}
 bool Blocked(bool foreground=true,bool cursor=false){return context::BlocksBoost(Ui(),encounter,foreground,cursor);}
};
int main(){unsigned checks=0,failed=0;auto check=[&](bool ok,const char* name){++checks;if(!ok){++failed;std::printf("FAIL %s\n",name);}};
 Fixture idle;check(idle.Ui().mode_requests==0x10&&idle.Ui().subtype_requests==0x20000,"diagnostics retain exact native request masks");check(!idle.Blocked(true,false),"focused ordinary gameplay allowed");check(!idle.Blocked(true,true),"visible OS cursor cannot block normal grounded/airborne gameplay");
 check(idle.Blocked(false,false),"foreign or absent foreground blocks keyboard/controller boost");
 idle.encounter.state=boss::State::Active;check(idle.Blocked(true,false),"actual boss blocks normal UI");idle.encounter.state=boss::State::Unknown;check(idle.Blocked(),"unknown boss remains blocked");
 for(uint8_t sub:{uint8_t(0),uint8_t(1),uint8_t(4),uint8_t(5),uint8_t(7),uint8_t(12),uint8_t(15),uint8_t(16),uint8_t(18)}){Fixture f;f.m.Put(0x10232,sub);check(f.Blocked(),"non-default native context blocks even with hidden OS cursor");}
 for(uint8_t mode:{uint8_t(0),uint8_t(1),uint8_t(2),uint8_t(3),uint8_t(5),uint8_t(6),uint8_t(7)}){Fixture f;f.m.Put(0x10230,mode);check(f.Blocked(),"non-game native UI mode blocks");}
 Fixture worldblock;worldblock.m.Put(0x10509,uint8_t(0));check(worldblock.Blocked(),"native gameplay inactive blocks while UI remains4/17");
 Fixture worldnonzero;worldnonzero.m.Put(0x10509,uint8_t(2));check(!worldnonzero.Blocked(),"native gameplay eligibility accepts any nonzero byte");
 Fixture worldinvalid;worldinvalid.m.Put(0x10448,uint8_t(7));check(worldinvalid.Blocked(),"invalid native world state blocks safely");
 Fixture crossed;crossed.encounter.world_root=0x10600;check(crossed.Blocked(),"boss and UI samples from different worlds cannot allow boost");
 Fixture queued;queued.m.Put(0x10280,uint32_t(1));check(queued.Blocked(),"queued modal request blocks before mode commit");
 Fixture dirty;dirty.m.Put(0x10266,uint8_t(1));
 check(dirty.Ui().state==context::UiState::Gameplay&&!dirty.Blocked(),"captured ordinary3460 4/17 cacheDirty1 queue0 is gameplay");
 Fixture captured;captured.m.Put(0x10448,uint8_t(5));captured.m.Put(0x10438,uintptr_t(0x10500));captured.m.Put(0x10266,uint8_t(1));
 check(!captured.Blocked(),"captured3460 worldState5 enabled1 4/17 dirty1 allows focused gameplay");
 check(captured.Blocked(false),"captured3460 snapshot still blocks when own foreground is false");
 Fixture dirtyshop;dirtyshop.m.Put(0x10266,uint8_t(1));dirtyshop.m.Put(0x10232,uint8_t(5));check(dirtyshop.Blocked(),"cacheDirty1 cannot allow committed shop context");
 Fixture dirtyrequest;dirtyrequest.m.Put(0x10266,uint8_t(1));dirtyrequest.m.Put(0x10246,uint8_t(1));check(dirtyrequest.Blocked(),"cacheDirty1 cannot allow prohibited request before commit");
 Fixture dirtyqueue;dirtyqueue.m.Put(0x10266,uint8_t(1));dirtyqueue.m.Put(0x10280,uint32_t(1));check(dirtyqueue.Blocked(),"cacheDirty1 queued callback remains blocked");
 Fixture invaliddirty;invaliddirty.m.Put(0x10266,uint8_t(2));check(invaliddirty.Ui().state==context::UiState::Unknown,"nonboolean native cacheDirty remains unknown");
 Fixture pending;pending.m.Put(0x10246,uint8_t(1));check(pending.Blocked(),"prohibited subtype request blocks while committed17 still visible");
 Fixture bad;bad.m.Put(0x10280,uint32_t(4097));check(bad.Ui().state==context::UiState::Unknown&&bad.Blocked(),"invalid queue count stays unknown and blocked");
 Fixture nullmode;nullmode.m.Put(0x10198,uintptr_t(0));check(nullmode.Blocked(),"missing UI mode pointer safely blocks");
 Fixture unsupported;unsupported.p.valid=false;check(unsupported.Blocked(),"unknown native profile blocks");
 Fixture changed;unsigned hits=0;changed.m.mutate=[&](uintptr_t at){if(at==0x10230 && ++hits==2)changed.m.Put(0x10232,uint8_t(5));};check(changed.Ui().state==context::UiState::Unknown,"mode mutation during sample is unknown");
 Fixture exit;exit.m.Put(0x10232,uint8_t(5));check(exit.Blocked(),"shop context active blocks");exit.m.Put(0x10232,uint8_t(17));check(!exit.Blocked(true,true),"native UI close allows new jump despite OS cursor remaining visible");
 std::printf("input context: %u checks, %u failures\n",checks,failed);return failed?1:0;}
