#pragma once
#include <Windows.h>
#include <array>
#include <vector>
#include <string>
#include <algorithm>
#include "TraceBridge.h"
#include "TraceQueue.h"
#include "../SuperJumpBossNativeProfile.h"
#include "../SuperJumpContextGuard.h"

namespace motiontrace::runtime {
inline bool SafeRead(void*,uintptr_t,void*,size_t) noexcept;
inline bool Readable(DWORD p) noexcept {
 if(p&(PAGE_GUARD|PAGE_NOACCESS))return false;
 switch(p&0xff){case PAGE_READONLY:case PAGE_READWRITE:case PAGE_WRITECOPY:case PAGE_EXECUTE_READ:case PAGE_EXECUTE_READWRITE:case PAGE_EXECUTE_WRITECOPY:return true;default:return false;}
}
inline bool SafeRead(void*,uintptr_t at,void*out,size_t n) noexcept {
 if(!out||!n||at<0x10000||at>=0x800000000000ULL||n>0x800000000000ULL-at)return false;
 size_t copied=0;
 while(copied<n){
  MEMORY_BASIC_INFORMATION m{};const uintptr_t current=at+copied;
  if(VirtualQuery(reinterpret_cast<const void*>(current),&m,sizeof(m))!=sizeof(m)||m.State!=MEM_COMMIT||!Readable(m.Protect))return false;
  const auto region=reinterpret_cast<uintptr_t>(m.BaseAddress);
  if(region>current||m.RegionSize>UINTPTR_MAX-region||region+m.RegionSize<=current)return false;
  const auto step=(std::min)(n-copied,static_cast<size_t>(region+m.RegionSize-current));
  __try{std::memcpy(static_cast<unsigned char*>(out)+copied,reinterpret_cast<const void*>(current),step);}
  __except(EXCEPTION_EXECUTE_HANDLER){return false;}
  copied+=step;
 }
 return true;
}
inline bool PrivateExecutable(void*,uintptr_t at,size_t n) noexcept {
 if(!n||at<0x10000||at>=0x800000000000ULL||n>0x800000000000ULL-at)return false;
 const void*allocation=nullptr;size_t checked=0;
 while(checked<n){
  MEMORY_BASIC_INFORMATION m{};const auto p=at+checked;
  if(VirtualQuery(reinterpret_cast<const void*>(p),&m,sizeof(m))!=sizeof(m)||m.State!=MEM_COMMIT||m.Type!=MEM_PRIVATE||(m.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
  const DWORD protect=m.Protect&0xff;
  if(protect!=PAGE_EXECUTE_READ&&protect!=PAGE_EXECUTE_READWRITE&&protect!=PAGE_EXECUTE_WRITECOPY)return false;
  if(allocation&&allocation!=m.AllocationBase)return false;allocation=m.AllocationBase;
  const auto start=reinterpret_cast<uintptr_t>(m.BaseAddress);
  if(start>p||m.RegionSize>UINTPTR_MAX-start||start+m.RegionSize<=p)return false;
  checked+=(std::min)(n-checked,static_cast<size_t>(start+m.RegionSize-p));
 }
 return true;
}
inline bool CanDrain(unsigned batch,uint64_t start,uint64_t now,bool armed){return armed&&batch<128&&now>=start&&now-start<900000;}
inline TraceTags MakeTags(const superjump::context::UiResult&ui,const superjump::boss::Result&boss,bool foreground){
 TraceTags t{};t.ui_state=static_cast<int>(ui.state);t.ui_reason=static_cast<int>(ui.reason);t.boss_state=static_cast<int>(boss.state);t.boss_reason=static_cast<int>(boss.reason);t.boss_uid=boss.boss_uid;t.ui_mode=ui.mode;t.ui_submode=ui.submode;t.foreground=foreground;
 t.roots_coherent=ui.world_root&&ui.world_root==boss.world_root;if(t.roots_coherent)t.coherent_world_root=ui.world_root;return t;
}
// The finite capture window starts at the first coherent typed gameplay sample.
class CaptureWindow {
 bool started_=false;uint64_t start_=0;
public:
 explicit CaptureWindow(uint64_t /*observer_start*/){}
 bool Started()const{return started_;}uint64_t StartTick()const{return start_;}
 bool Expired(uint64_t now)const{return started_&&(now<start_||now-start_>=900000);}
 bool MayDrain(unsigned batch,uint64_t now,bool armed)const{return armed&&batch<128&&!Expired(now);}
 bool Observe(const Snapshot&s,const TraceTags&t,uint64_t now){
  if(started_||!s.valid||s.hero==Hero::Unknown||!s.actor||!s.uid||!s.root||t.ui_state!=static_cast<int>(superjump::context::UiState::Gameplay)||!t.roots_coherent||t.coherent_world_root!=s.root)return false;
  start_=now;started_=true;return true;
 }
};
inline bool SameMotion(const Snapshot&a,const Snapshot&b){
 return a.valid==b.valid&&a.reason==b.reason&&a.hero==b.hero&&a.root==b.root&&a.context==b.context&&a.actor==b.actor&&a.uid==b.uid&&a.hero_key==b.hero_key&&a.controller==b.controller&&a.movement_state==b.movement_state&&a.ground_known==b.ground_known&&a.grounded==b.grounded&&a.jump_request==b.jump_request&&a.jump_blocked==b.jump_blocked&&a.upper==b.upper&&a.lower==b.lower;
}
inline bool SameTags(const TraceTags&a,const TraceTags&b){return a.ui_state==b.ui_state&&a.ui_reason==b.ui_reason&&a.boss_state==b.boss_state&&a.boss_reason==b.boss_reason&&a.boss_uid==b.boss_uid&&a.ui_mode==b.ui_mode&&a.ui_submode==b.ui_submode&&a.foreground==b.foreground&&a.roots_coherent==b.roots_coherent&&a.coherent_world_root==b.coherent_world_root;}
}
