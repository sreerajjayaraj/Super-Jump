#pragma once
#include "TracePolicy.h"
#include "../PalmPolicy.h"
#include <atomic>
#include <type_traits>
namespace motiontrace {
struct TraceTags{int ui_state=0,ui_reason=0,boss_state=0,boss_reason=0;uint32_t boss_uid=0;uint8_t ui_mode=0xff,ui_submode=0xff;bool foreground=false,roots_coherent=false;uintptr_t coherent_world_root=0;};
enum class PalmBindStage : uint8_t {
 Unset, Sampler, Disabled, EpochPending, Hero, Ui, Boss, Foreground, Roots, WorldRoot,
 Traversal, Owner, BossRecheck, BossOwner, Profile, GenerationChanged, EpochRecheck, Bound
};
struct TraceEvent{
 uint64_t sequence=0,tick=0;
 unsigned site=0;
 uint8_t native_success=0;
 // Diagnostic bytes use the existing alignment gap before native_state.
 PalmBindStage palm_bind_stage=PalmBindStage::Unset;
 // Traversal reason, or an owner mismatch mask if palm_bind_stage is Owner.
 uint8_t palm_traversal_reason=UINT8_MAX,palm_profile_reason=UINT8_MAX;
 uintptr_t native_state=0;
 Snapshot snapshot{};
 TraceTags tags{};
 superjump::palm::Event palm{};
};
static_assert(std::is_trivially_copyable_v<TraceEvent>);
struct DeliveryCursor{uint64_t last=0;bool Accept(uint64_t sequence){if(!sequence||sequence<=last)return false;last=sequence;return true;}};
template<size_t N>class BoundedQueue{
 static_assert(N>0);std::atomic_flag guard=ATOMIC_FLAG_INIT;std::array<TraceEvent,N>packets{};size_t begin=0,count=0;uint64_t next=0;std::atomic<uint64_t>dropped=0;
public:
 bool Push(const TraceEvent&packet){if(guard.test_and_set(std::memory_order_acquire)){dropped.fetch_add(1,std::memory_order_relaxed);return false;}if(count==N||next==UINT64_MAX){guard.clear(std::memory_order_release);dropped.fetch_add(1,std::memory_order_relaxed);return false;}auto&out=packets[(begin+count)%N];out=packet;out.sequence=++next;out.palm.sequence=out.sequence;out.palm.tick=out.tick;++count;guard.clear(std::memory_order_release);return true;}
 bool Pop(TraceEvent&packet){if(guard.test_and_set(std::memory_order_acquire))return false;if(!count){guard.clear(std::memory_order_release);return false;}packet=packets[begin];begin=(begin+1)%N;--count;guard.clear(std::memory_order_release);return true;}
 uint64_t Dropped()const{return dropped.load(std::memory_order_relaxed);}
};
}
