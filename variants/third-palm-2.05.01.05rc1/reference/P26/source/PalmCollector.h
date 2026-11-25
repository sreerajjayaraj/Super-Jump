#pragma once
#include "native-trace/TraceObserver.h"
#include "native-trace/RuntimeSupport.h"
#include "native-trace/TraceVerificationDiagnostics.h"
#include "PalmNativeContracts.h"
#include "PalmCollectorSites.h"
#include "PalmMenuModeContract.h"
#include "AxiomDelayContract.h"
#include "SuperJumpHostPolicy.h"
#include "safetyhook/mid_hook.hpp"
#include <new>
#include <utility>
namespace superjump::palm {
// This object and its callback state must have process lifetime. Initialize pins
// this ASI; prepared/active hooks are retained on every failure. No hot unhook.
class Collector {
public:
 using Reader=motiontrace::Reader;using Profile=motiontrace::Profile;using TraceEvent=motiontrace::TraceEvent;
 using BindFn=motiontrace::Observer::BindFn;using TagsFn=motiontrace::Observer::TagsFn;using VerifyResult=motiontrace::Observer::VerifyResult;
 enum class State{Idle,Initializing,Running,Stopped,HostRefused,Arguments,RecorderConflict,CollectorConflict,ProfileRefused,ProducerOrContract,PrepareHook,EnableHook,OwnedBridge,PinRefused,ClockRefused};
 struct PrepareFailure {
    enum class Step {None,Create,AllocateObject,InstructionSpan,BridgeAddress};
    Step step=Step::None;
    unsigned site=0, type=0, detail=0, allocator=0;
    size_t stolen=0;
 };
private:
 PrepareFailure prepare_failure_{};
 uintptr_t base_=0;Reader reader_{};Profile profile_{};
 std::array<safetyhook::MidHook*,2>hooks_{};std::array<motiontrace::OwnPatch,2>patches_{};
 std::array<uint32_t,2>sites_{};
 std::array<uintptr_t,2>stub_{},trampoline_{};motiontrace::Observer observer_{};motiontrace::diagnostics::FirstFailure first_failure_{};
 std::atomic<State>state_{State::Idle};
 menu_mode::Provider menu_mode_owner_{};std::atomic<bool>menu_mode_proven_{false};
#ifdef PALM_COLLECTOR_TESTING
 motiontrace::ExecFn menu_mode_exec_=motiontrace::runtime::PrivateExecutable;void*menu_mode_exec_context_=nullptr;
#endif
 HANDLE lease_=nullptr;uint64_t next_check_=0,busy_since_=0,last_now_=0;
 inline static std::atomic<Collector*>current_{nullptr};
 inline static constexpr std::array<uint8_t,5>original_={0x4c,0x89,0x7c,0x24,0x28};
 static void Callback0(safetyhook::Context&ctx){if(auto*c=current_.load(std::memory_order_acquire))c->observer_.Observe(ctx,0);}
 static void Callback1(safetyhook::Context&ctx){if(auto*c=current_.load(std::memory_order_acquire))c->observer_.Observe(ctx,1);}
 inline static constexpr std::array<safetyhook::MidHookFn,2>callbacks_={Callback0,Callback1};
 void Record(motiontrace::diagnostics::Failure f)noexcept{
  f.thread=GetCurrentThreadId();f.failureTick=GetTickCount64();
  if(f.execAddress){MEMORY_BASIC_INFORMATION m{};if(VirtualQuery(reinterpret_cast<const void*>(f.execAddress),&m,sizeof(m))==sizeof(m)){f.vmState=m.State;f.vmType=m.Type;f.vmProtect=m.Protect;f.allocationBase=reinterpret_cast<uintptr_t>(m.AllocationBase);f.vmSampleAfterFailure=true;f.vmSampleTick=GetTickCount64();}}
  (void)first_failure_.Publish(f);
 }
 bool Refuse(State s)noexcept{observer_.Stop();state_.store(s,std::memory_order_release);return false;}
 VerifyResult Contracts(size_t count){
  using namespace motiontrace;
  static_assert(superjump::traversal::native::WitnessesAxiomDelay(motiontrace::native::kBlocks));
  static_assert(superjump::traversal::native::WitnessesAxiomDelay(
      motiontrace::native::shifted::kBlocks,superjump::traversal::native::kShiftedAxiomDelayRva));
  const bool shifted=profile_.shifted_layout;
  const auto modeRva=shifted?menu_mode::kShiftedModeRva:menu_mode::kModeRva;
  diagnostics::Failure failure{};
  if(count>2)return VerifyResult::Invalid;
#ifdef PALM_COLLECTOR_TESTING
  menu_mode::View mode(reader_,base_,menu_mode_owner_,menu_mode_exec_,menu_mode_exec_context_,modeRva);
#else
  menu_mode::View mode(reader_,base_,menu_mode_owner_,motiontrace::runtime::PrivateExecutable,nullptr,modeRva);
#endif
  superjump::traversal::native::AxiomDelayView axiom{mode.Wrapped(),base_,shifted};
  const Reader normalized=axiom.Wrapped();
  bool valid=diagnostics::Contracts(normalized,base_,count,std::span<const OwnPatch>(patches_.data(),count),failure,shifted,sites_);
  const auto extra=shifted?std::span<const ContractBlock>{native::shifted::kExtraContracts}:
                           std::span<const ContractBlock>{native::kExtraContracts};
  if(valid){
   diagnostics::Audit audit(normalized);valid=CheckContracts(audit.Wrapped(),base_,extra,{});
   if(!valid){failure=diagnostics::Describe(diagnostics::Stage::NativeContracts,audit,base_,[&](uintptr_t at,uint8_t&value){for(const auto&b:extra)if(at>=base_+b.rva&&at-(base_+b.rva)<b.size){value=b.bytes[at-(base_+b.rva)];return true;}return diagnostics::Expected(base_,at,std::span<const OwnPatch>(patches_.data(),count),value,shifted);});for(size_t i=0;i<extra.size();++i){const auto&b=extra[i];if(failure.address>=base_+b.rva&&failure.address-(base_+b.rva)<b.size){failure.block=static_cast<uint32_t>(diagnostics::NativeBlocks(shifted).size()+i);break;}}}
  }
  if(valid){menu_mode_proven_.store(mode.Normalized(),std::memory_order_relaxed);return VerifyResult::Valid;}
  Record(failure);return VerifyResult::Invalid;
 }
 bool Bridge(unsigned i){
  using namespace motiontrace;diagnostics::Failure failure{};
  if(i>=2||!hooks_[i]||hooks_[i]->destination()!=callbacks_[i]){failure.stage=diagnostics::Stage::Object;failure.detail=diagnostics::Detail::Ownership;failure.site=i;if(i<2){failure.scalarKnown=true;failure.expectedValue=reinterpret_cast<uintptr_t>(callbacks_[i]);if(hooks_[i])failure.actualValue=reinterpret_cast<uintptr_t>(hooks_[i]->destination());}Record(failure);return false;}
  diagnostics::BridgeSpec spec{};spec.site=base_+sites_[i];spec.callback=reinterpret_cast<uintptr_t>(callbacks_[i]);spec.stub=stub_[i];spec.trampoline=trampoline_[i];spec.original=original_;spec.patch[0]=0xe9;
  const auto delta=static_cast<intptr_t>(spec.trampoline+10)-static_cast<intptr_t>(spec.site+5);if(delta>=INT32_MIN&&delta<=INT32_MAX){const auto displacement=static_cast<int32_t>(delta);memcpy(spec.patch.data()+1,&displacement,4);}if(patches_[i].size==5)memcpy(spec.patch.data(),patches_[i].bytes.data(),5);
  if(diagnostics::Bridge(reader_,base_,i,spec,motiontrace::runtime::PrivateExecutable,nullptr,failure))return true;Record(failure);return false;
 }
 VerifyResult Verify(){
  using namespace motiontrace;const auto valid=Contracts(2);if(valid!=VerifyResult::Valid)return valid;
  for(unsigned i=0;i<2;++i){std::array<uint8_t,5>actual{};const bool read=reader_.Bytes(base_+sites_[i],actual.data(),actual.size());if(!read||memcmp(actual.data(),patches_[i].bytes.data(),5)){diagnostics::Failure f{};f.stage=diagnostics::Stage::OwnedSite;f.detail=read?diagnostics::Detail::BytesChanged:diagnostics::Detail::ReadFailed;f.site=i;f.rva=sites_[i];f.address=base_+f.rva;f.readAddress=f.address;f.readSize=5;f.readOk=read;f.samples=read?5u:0u;f.known=31;memcpy(f.expected.data(),patches_[i].bytes.data(),5);if(read)memcpy(f.actual.data(),actual.data(),5);Record(f);return VerifyResult::Invalid;}if(!Bridge(i))return VerifyResult::Invalid;}
  return VerifyResult::Valid;
 }
 static VerifyResult VerifyThunk(void*opaque){return static_cast<Collector*>(opaque)->Verify();}
 bool Wait(size_t count,bool paired){const auto start=GetTickCount64();for(;;){const auto v=paired?Verify():Contracts(count);if(v==VerifyResult::Valid)return true;if(v==VerifyResult::Invalid)return false;if(GetTickCount64()-start>=5000){motiontrace::diagnostics::Failure f{};f.stage=motiontrace::diagnostics::Stage::NativeContracts;f.detail=motiontrace::diagnostics::Detail::ShapeOrOpaque;Record(f);return false;}Sleep(1);}}
#ifdef PALM_COLLECTOR_TESTING
 friend struct CollectorTestAccess;
#endif
public:
 Collector()=default;Collector(const Collector&)=delete;Collector&operator=(const Collector&)=delete;
 bool Initialize(uintptr_t base,const Reader&reader,const Profile&profile,BindFn bind,void*bindContext,TagsFn tags=nullptr,void*tagsContext=nullptr,const menu_mode::Provider* retainedMenu=nullptr){
  State idle=State::Idle;if(!state_.compare_exchange_strong(idle,State::Initializing,std::memory_order_acq_rel))return false;
  const auto game=host::GameExecutable();
  if(!game||base!=reinterpret_cast<uintptr_t>(game))return Refuse(State::HostRefused);
  if(!reader.read||!profile.valid||!bind)return Refuse(State::Arguments);
  if(GetModuleHandleW(L"SJNativeMotionTrace.asi"))return Refuse(State::RecorderConflict);
  wchar_t name[112]{};swprintf_s(name,L"Local\\CrimsonDesert.SuperJump.PalmCollector.%lu",GetCurrentProcessId());SetLastError(ERROR_SUCCESS);lease_=CreateMutexW(nullptr,FALSE,name);if(!lease_)return Refuse(State::CollectorConflict);if(GetLastError()==ERROR_ALREADY_EXISTS){CloseHandle(lease_);lease_=nullptr;return Refuse(State::CollectorConflict);}
  HMODULE pinned=nullptr;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&Callback0),&pinned))return Refuse(State::PinRefused);
  Collector*empty=nullptr;if(!current_.compare_exchange_strong(empty,this,std::memory_order_acq_rel))return Refuse(State::CollectorConflict);
  base_=base;reader_=reader;profile_=superjump::boss::ValidateProfile(reader_,base_);
  if(!profile_.valid||profile.base!=profile_.base||profile.root_slot!=profile_.root_slot||profile.info_slot!=profile_.info_slot||profile.player_vtable!=profile_.player_vtable||profile.actor_root_slot!=profile_.actor_root_slot)return Refuse(State::ProfileRefused);
  menu_mode_owner_=retainedMenu?*retainedMenu:menu_mode::RetainExactMenu(reader_);
  const auto movementDeadline=GetTickCount64()+15000;
  while(!menu60::LocomotionReady(reader_,base_,menu_mode_owner_.Movement(),menu60::PrivateExecutable,profile_.shifted_layout)){
   if(GetTickCount64()>=movementDeadline) return Refuse(State::ProducerOrContract);
   Sleep(25);
  }
  sites_=LocateCollectorSites(reader_,base_,[&](uintptr_t at,size_t size) {
    return host::IsGameCode(game,at,size);
  });
  if (!sites_[0] || !sites_[1]) return Refuse(State::ProducerOrContract);
  if(!Wait(0,false))return Refuse(State::ProducerOrContract);
  const auto allocator=host::HookAllocator(game);
  if (!allocator) {
   prepare_failure_={PrepareFailure::Step::Create,0,0,0,static_cast<unsigned>(safetyhook::Allocator::Error::NO_MEMORY_IN_RANGE)};
   return Refuse(State::PrepareHook);
  }
  for (unsigned i=0;i<2;++i) {
   using Step=PrepareFailure::Step;
   using MidError=safetyhook::MidHook::Error;
   using InlineError=safetyhook::InlineHook::Error;
   const auto target=base_+sites_[i];
   if (!host::IsGameCode(game,target,original_.size()))
    return Refuse(State::ProducerOrContract);
   auto h=safetyhook::MidHook::create(allocator,reinterpret_cast<void*>(target),
                                     callbacks_[i],safetyhook::MidHook::StartDisabled);
   if (!h) {
    const auto error=h.error();
    prepare_failure_={Step::Create,i,unsigned(error.type)};
    if (error.type==MidError::BAD_ALLOCATION)
     prepare_failure_.allocator=unsigned(error.allocator_error);
    else {
     prepare_failure_.detail=unsigned(error.inline_hook_error.type);
     if (error.inline_hook_error.type==InlineError::BAD_ALLOCATION)
      prepare_failure_.allocator=unsigned(error.inline_hook_error.allocator_error);
    }
    return Refuse(State::PrepareHook);
   }
   hooks_[i]=new(std::nothrow)safetyhook::MidHook(std::move(*h));
   if (!hooks_[i]) {
    prepare_failure_={Step::AllocateObject,i};
    return Refuse(State::PrepareHook);
   }
   const auto& original=hooks_[i]->original_bytes();
   if (original.size()!=original_.size()||memcmp(original.data(),original_.data(),original_.size())) {
    prepare_failure_={Step::InstructionSpan,i,0,0,0,original.size()};
    return Refuse(State::PrepareHook);
   }
   stub_[i]=hooks_[i]->owned_stub_address();
   trampoline_[i]=hooks_[i]->owned_trampoline_address();
   if (!stub_[i]||!trampoline_[i]) {
    prepare_failure_={Step::BridgeAddress,i};
    return Refuse(State::PrepareHook);
   }
  }
  for(unsigned i=0;i<2;++i){if(!Wait(i,false))return Refuse(State::ProducerOrContract);if(!hooks_[i]->enable())return Refuse(State::EnableHook);if(!Bridge(i))return Refuse(State::OwnedBridge);auto&p=patches_[i];p.rva=sites_[i];p.size=5;if(!reader_.Bytes(base_+p.rva,p.bytes.data(),5)||p.bytes[0]!=0xe9)return Refuse(State::OwnedBridge);}
  if(!Wait(2,true))return Refuse(State::OwnedBridge);
  observer_.ConfigureResult(reader_,profile_,VerifyThunk,this,tags,tagsContext);observer_.ConfigureBinder(bind,bindContext);if(!observer_.Arm())return Refuse(State::Arguments);
  last_now_=GetTickCount64();next_check_=last_now_+1000;state_.store(State::Running,std::memory_order_release);return true;
 }
 bool Poll(){
  if(state_.load(std::memory_order_acquire)!=State::Running)return false;if(!observer_.Armed())return Refuse(State::ProducerOrContract);
  const auto now=GetTickCount64();if(now<last_now_)return Refuse(State::ClockRefused);last_now_=now;if(now<next_check_)return true;
  const auto result=Verify();if(result==VerifyResult::Busy){observer_.InvalidatePending();if(!busy_since_)busy_since_=now;if(now-busy_since_<5000)return true;return Refuse(State::ProducerOrContract);}busy_since_=0;if(result!=VerifyResult::Valid)return Refuse(State::ProducerOrContract);next_check_=now+1000;return true;
 }
 void Stop(){observer_.Stop();auto s=state_.load(std::memory_order_acquire);if(s==State::Running||s==State::Initializing)state_.store(State::Stopped,std::memory_order_release);}
 PrepareFailure PrepareDiagnostic()const{return prepare_failure_;}
 menu_mode::Provider MenuProvider()const{return menu_mode_owner_;}
 const char* ModeContractName()const{return menu_mode_proven_.load(std::memory_order_relaxed)&&menu_mode_owner_?menu_mode_owner_.profile->name:nullptr;}
 bool Armed()const{return observer_.Armed();}bool Pop(TraceEvent&e){return observer_.Pop(e);}
 uint64_t Dropped()const{return observer_.Dropped();}uint64_t CancellationEpoch()const{return observer_.CancellationEpoch();}
 uint64_t NativeSuccess()const{return observer_.NativeSuccess();}uint64_t NativeFalse()const{return observer_.NativeFalse();}uint64_t ContractFailures()const{return observer_.ContractFailures();}unsigned Inflight()const{return observer_.Inflight();}
 bool FirstFailure(motiontrace::diagnostics::Failure&f)const{return first_failure_.Read(f);}
 State Status()const{const auto s=state_.load(std::memory_order_acquire);return s==State::Running&&!observer_.Armed()?State::ProducerOrContract:s;}
 const char*ErrorText()const{switch(Status()){case State::Idle:return "idle";case State::Initializing:return "initializing";case State::Running:return "running";case State::Stopped:return "stopped-pinned-inert";case State::HostRefused:return "exact-host-refused";case State::Arguments:return "invalid-binder-profile-arguments";case State::RecorderConflict:return "SJNativeMotionTrace-conflicts-with-paired-native-sites";case State::CollectorConflict:return "another-palm-collector-already-owns-this-process";case State::ProfileRefused:return "exact-native-profile-pdata-refused";case State::ProducerOrContract:return "native-motion-contract-refused";case State::PrepareHook:return "prepare-near-paired-hooks-refused";case State::EnableHook:return "enable-paired-hook-refused";case State::OwnedBridge:return "exact-paired-owned-bridge-refused";case State::PinRefused:return "process-lifetime-module-pin-refused";case State::ClockRefused:return "monotonic-clock-refused";}return "unknown-refused";}
};
}
