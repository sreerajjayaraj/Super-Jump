#pragma once
#include "TraceQueue.h"
#include <Windows.h>
#include <immintrin.h>
#include "safetyhook/context.hpp"
namespace motiontrace {
class Observer {
public:enum class VerifyResult{Valid,Busy,Invalid};using VerifyFn=bool(*)(void*);using VerifyResultFn=VerifyResult(*)(void*);using TagsFn=TraceTags(*)(void*,const Reader&,const Profile&);using BindFn=bool(*)(void*,const Reader&,const Profile&,TraceEvent&)noexcept;
private:
 Reader reader_{};Profile profile_{};VerifyFn verify_=nullptr;VerifyResultFn verify_result_=nullptr;void*verify_context_=nullptr;TagsFn tags_=nullptr;void*tags_context_=nullptr;BindFn bind_=nullptr;void*bind_context_=nullptr;
 std::atomic<bool>armed_=false;std::atomic<unsigned>inflight_=0;std::atomic<uint64_t>native_false_=0,native_success_=0,contract_failures_=0,cancellation_epoch_=0;BoundedQueue<128>queue_{};
 struct Flight{std::atomic<unsigned>&counter;explicit Flight(std::atomic<unsigned>&x):counter(x){counter.fetch_add(1,std::memory_order_acq_rel);}~Flight(){counter.fetch_sub(1,std::memory_order_release);}};
 struct NativeStatus{DWORD error;unsigned csr;NativeStatus():error(GetLastError()),csr(_mm_getcsr()){}~NativeStatus(){_mm_setcsr(csr);SetLastError(error);}};
public:
 void Configure(Reader r,Profile p,VerifyFn verify,void*context,TagsFn tags=nullptr,void*tags_context=nullptr){if(Armed()||Inflight())return;reader_=r;profile_=p;verify_=verify;verify_result_=nullptr;verify_context_=context;tags_=tags;tags_context_=tags_context;}
 void ConfigureResult(Reader r,Profile p,VerifyResultFn verify,void*context,TagsFn tags=nullptr,void*tags_context=nullptr){if(Armed()||Inflight())return;reader_=r;profile_=p;verify_=nullptr;verify_result_=verify;verify_context_=context;tags_=tags;tags_context_=tags_context;}
 void ConfigureBinder(BindFn bind,void*context){if(Armed()||Inflight())return;bind_=bind;bind_context_=context;}
 bool Arm(){if(!profile_.valid||!reader_.read||(!verify_&&!verify_result_))return false;armed_.store(true,std::memory_order_release);return true;}
 void InvalidatePending(){cancellation_epoch_.fetch_add(1,std::memory_order_release);}
 void Stop(){if(armed_.exchange(false,std::memory_order_acq_rel))cancellation_epoch_.fetch_add(1,std::memory_order_release);}
 bool Armed()const{return armed_.load(std::memory_order_acquire);}
 void Observe(const safetyhook::Context&ctx,unsigned site)noexcept{
  if(site>1||!Armed())return;Flight flight(inflight_);if(!Armed())return;
  if((ctx.rax&0xff)!=1){native_false_.fetch_add(1,std::memory_order_relaxed);return;}
  NativeStatus preserve;native_success_.fetch_add(1,std::memory_order_relaxed);
  const auto verified=verify_result_?verify_result_(verify_context_):(verify_(verify_context_)?VerifyResult::Valid:VerifyResult::Invalid);
  if(verified==VerifyResult::Busy){cancellation_epoch_.fetch_add(1,std::memory_order_release);return;}
  if(verified==VerifyResult::Invalid){contract_failures_.fetch_add(1,std::memory_order_relaxed);Stop();return;}if(!Armed())return;
  TraceEvent e{};e.tick=GetTickCount64();e.site=site;e.native_success=1;e.native_state=ctx.rsi;e.snapshot=Sample(reader_,profile_,ctx.rsi);
  if(tags_)e.tags=tags_(tags_context_,reader_,profile_);
  if(bind_){
   // The binder may only contribute a fixed payload. Native metadata stays owned
   // here, even if an erroneous binder writes other fields in its local copy.
   TraceEvent candidate=e;const bool bound=e.snapshot.valid&&bind_(bind_context_,reader_,profile_,candidate);
   e.palm_bind_stage=e.snapshot.valid?candidate.palm_bind_stage:PalmBindStage::Sampler;
   e.palm_traversal_reason=candidate.palm_traversal_reason;
   e.palm_profile_reason=candidate.palm_profile_reason;
   e.palm=candidate.palm;e.palm.tick=e.tick;e.palm.sequence=0;e.palm.success=true;e.palm.grounded=e.snapshot.ground_known&&e.snapshot.grounded;e.palm.bound=bound&&candidate.palm.bound;
   if(!e.palm.bound)cancellation_epoch_.fetch_add(1,std::memory_order_release);
  }
  if(Armed()&&!queue_.Push(e))cancellation_epoch_.fetch_add(1,std::memory_order_release);
 }
 bool Pop(TraceEvent&e){return queue_.Pop(e);}uint64_t Dropped()const{return queue_.Dropped();}
 uint64_t CancellationEpoch()const{return cancellation_epoch_.load(std::memory_order_acquire);}
 uint64_t NativeFalse()const{return native_false_.load(std::memory_order_relaxed);}uint64_t NativeSuccess()const{return native_success_.load(std::memory_order_relaxed);}uint64_t ContractFailures()const{return contract_failures_.load(std::memory_order_relaxed);}unsigned Inflight()const{return inflight_.load(std::memory_order_acquire);}
};
}
