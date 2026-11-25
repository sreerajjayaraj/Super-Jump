#pragma once
#include <cstdint>
#include <initializer_list>
namespace superjump::palm {
enum class Mode{None,DoubleJump,Palm};
constexpr Mode Route(uint32_t key){return key==1?Mode::Palm:key==4||key==6?Mode::DoubleJump:Mode::None;}
constexpr int DoubleJumpMode(int configured){return configured==1?1:2;}
enum class Action{Unknown,Ground,InitialJump,Palm1,Palm2,Palm3,GroundPalm,OtherAir,Fall,Freeclimb,Flight,Swimming};
struct Identity{uintptr_t root=0,context=0,actor=0,controller=0,movement=0,component=0,body=0;uint32_t uid=0,hero=0;int32_t generation=0;bool operator==(const Identity&)const=default;};
struct Context{Identity identity{};uint64_t tick=0;bool valid=false,allowed=false,grounded=false;Action action=Action::Unknown;};
struct Event{Identity identity{};uint64_t tick=0,sequence=0;bool success=false,bound=false;Action action=Action::Unknown;bool grounded=false;};
enum class Phase{Idle,Initial,Palm1,Palm2,Consumed};
class Policy {
 Identity identity_{};Phase phase_=Phase::Idle;bool context_=false,ready_=false;uint64_t last_now_=0,last_native_tick_=0,last_sequence_=0,reset_fence_=0;
 void Cancel(uint64_t now){phase_=Phase::Idle;ready_=false;if(now>reset_fence_)reset_fence_=now;}
 static bool Plausible(const Identity&x){return Route(x.hero)==Mode::Palm&&x.root&&x.context&&x.actor&&x.controller&&x.movement&&x.component&&x.body&&x.uid;}
 static bool IsPalm(Action a){return a==Action::Palm1||a==Action::Palm2||a==Action::Palm3;}
 bool CurrentAllowed(Action action)const{switch(phase_){case Phase::Initial:return action==Action::InitialJump||action==Action::Palm1;case Phase::Palm1:return action==Action::Palm1||action==Action::Palm2;case Phase::Palm2:return action==Action::Palm2||action==Action::Palm3;case Phase::Consumed:return IsPalm(action);case Phase::Idle:return true;}return false;}
public:
 Phase CurrentPhase()const{return phase_;}bool Ready()const{return ready_;}
 void Reset(uint64_t now=0){if(!now)now=last_now_;identity_={};Cancel(now);context_=false;last_now_=now;last_native_tick_=last_sequence_=0;}
 void ObserveContext(const Context&x,bool queuedForward=false){
  if(x.tick<last_now_||!x.valid||!x.allowed||!Plausible(x.identity)){Reset(x.tick);return;}
  last_now_=x.tick;
  if(!context_||!(identity_==x.identity)){Cancel(x.tick);identity_=x.identity;context_=true;last_native_tick_=last_sequence_=0;}
  const bool forward=queuedForward&&(x.action==Action::InitialJump||IsPalm(x.action));
  if((!CurrentAllowed(x.action)&&!forward)||(phase_!=Phase::Idle&&x.grounded&&IsPalm(x.action)))Cancel(x.tick);
  if(phase_==Phase::Idle){
   if(x.action==Action::Ground)ready_=x.grounded;
   else if(x.action==Action::InitialJump)ready_=ready_||x.grounded;
   else if(!(forward&&IsPalm(x.action)&&!x.grounded))ready_=false;
  }
 }
 bool OnNative(const Event&e,uint64_t now){
  if(!e.success)return false;
  if(!context_||!e.bound||!Plausible(e.identity)||!(identity_==e.identity)||now<last_now_||e.tick>now||e.tick<=reset_fence_||now-e.tick>50||e.sequence<=last_sequence_||(last_sequence_&&e.tick<last_native_tick_)){Cancel(now);return false;}
  last_now_=now;last_native_tick_=e.tick;last_sequence_=e.sequence;
  if(IsPalm(e.action)&&e.grounded){Cancel(now);return false;}
  if(e.action==Action::InitialJump){if(phase_==Phase::Initial)return false;if(phase_==Phase::Idle&&ready_&&e.grounded){phase_=Phase::Initial;ready_=false;return false;}Cancel(now);return false;}
  if(e.action==Action::Palm1){if(phase_==Phase::Palm1)return false;if(phase_==Phase::Initial){phase_=Phase::Palm1;return false;}Cancel(now);return false;}
  if(e.action==Action::Palm2){if(phase_==Phase::Palm2)return false;if(phase_==Phase::Palm1){phase_=Phase::Palm2;return false;}Cancel(now);return false;}
  if(e.action==Action::Palm3){if(phase_==Phase::Consumed)return false;if(phase_==Phase::Palm2){phase_=Phase::Consumed;return true;}Cancel(now);return false;}
  Cancel(now);return false;
 }
};
}
