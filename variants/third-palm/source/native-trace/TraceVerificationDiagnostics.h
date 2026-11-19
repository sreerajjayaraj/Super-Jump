#pragma once
#include "TraceNativeProfile.h"
#include "TraceBridge.h"
#include "DiagnosticExpected.h"
#include <atomic>
#include <algorithm>
#include <utility>
namespace motiontrace::diagnostics {
enum class Stage:uint32_t {None,NativeContracts,NativeBindings,BossProfile,OwnedSite,Bridge,Object};
enum class Detail:uint32_t {None,ReadFailed,BytesChanged,ShapeOrOpaque,PrivateExecutable,Ownership};
struct Failure {
 Stage stage=Stage::None;Detail detail=Detail::None;uint32_t site=UINT32_MAX,block=UINT32_MAX,rva=UINT32_MAX;uintptr_t address=0,readAddress=0;size_t readSize=0;
 std::array<uint8_t,16>expected{},actual{};uint16_t known=0;unsigned samples=0;bool readOk=false,scalarKnown=false;
 uintptr_t execAddress=0;size_t execSize=0;uintptr_t expectedStub=0,actualStub=0,expectedTrampoline=0,actualTrampoline=0;
 uint64_t failureTick=0,vmSampleTick=0;uintptr_t expectedValue=0,actualValue=0;uint32_t thread=0,vmState=0,vmType=0,vmProtect=0;uintptr_t allocationBase=0;bool vmSampleAfterFailure=false;
};
class FirstFailure {
 std::atomic<unsigned>state_{0};Failure value_{};
#ifdef MOTIONTRACE_DIAGNOSTIC_TESTING
 friend struct FirstFailureTestAccess;
#endif
public:
 bool Publish(const Failure&f)noexcept{unsigned empty=0;if(f.stage==Stage::None||!state_.compare_exchange_strong(empty,1,std::memory_order_acq_rel))return false;value_=f;state_.store(2,std::memory_order_release);return true;}
 bool Pending()const noexcept{return state_.load(std::memory_order_acquire)==1;}
 bool Read(Failure&f)const noexcept{if(state_.load(std::memory_order_acquire)!=2)return false;f=value_;return true;}
};
enum class RecordStatus {Empty,Ready,PendingTimeout};
template<class Clock,class Pause>inline RecordStatus WaitForRecord(const FirstFailure&store,Failure&out,Clock now,Pause pause){
 const uint64_t start=now();unsigned pauses=0;
 for(;;){if(store.Read(out))return RecordStatus::Ready;if(!store.Pending())return RecordStatus::Empty;const auto tick=now();if(tick<start||tick-start>=100||pauses>=100)return RecordStatus::PendingTimeout;++pauses;pause();}
}
struct Audit {
 Reader backing{};uintptr_t at=0;size_t size=0,captured=0;bool ok=false,seen=false;std::array<uint8_t,1024>data{};
 explicit Audit(Reader r):backing(r){}
 static bool Read(void*p,uintptr_t at,void*out,size_t n){auto&a=*static_cast<Audit*>(p);a.at=at;a.size=n;a.seen=true;a.ok=a.backing.Bytes(at,out,n);a.captured=a.ok?(std::min)(n,a.data.size()):0;if(a.captured)std::memcpy(a.data.data(),out,a.captured);return a.ok;}
 Reader Wrapped(){return {this,Read};}
};
inline bool Expected(uintptr_t base,uintptr_t at,std::span<const OwnPatch>patches,uint8_t&v){
 if(at<base)return false;const auto rva=at-base;
 for(const auto&b:native::kBlocks)if(rva>=b.rva&&rva-b.rva<b.size){v=b.bytes[rva-b.rva];if(b.allow_own_patch)for(const auto&p:patches)if(rva>=p.rva&&rva-p.rva<p.size)v=p.bytes[rva-p.rva];return true;}
 for(const auto&f:kFields)if(rva>=f.rva&&rva-f.rva<f.size){const uint64_t value=f.pointer?base+f.value:f.value;v=static_cast<uint8_t>(value>>((rva-f.rva)*8));return true;}
 return false;
}
template<class ExpectedFn>inline Failure Describe(Stage stage,const Audit&a,uintptr_t base,ExpectedFn expected){
 Failure f{};f.stage=stage;f.detail=!a.seen?Detail::ShapeOrOpaque:!a.ok?Detail::ReadFailed:Detail::ShapeOrOpaque;f.address=a.at;f.readAddress=a.at;f.readSize=a.size;f.readOk=a.ok;
 if(a.ok&&a.captured==a.size&&a.size<=8){f.scalarKnown=true;for(size_t i=0;i<a.size;++i){uint8_t v=0;f.actualValue|=uint64_t(a.data[i])<<(i*8);if(expected(a.at+i,v))f.expectedValue|=uint64_t(v)<<(i*8);else f.scalarKnown=false;}}
 size_t first=0;bool changed=false;
 for(size_t i=0;i<a.captured;++i){uint8_t v=0;if(expected(a.at+i,v)&&v!=a.data[i]){first=i;changed=true;break;}}
 if(changed){f.detail=Detail::BytesChanged;f.address=a.at+first;}
 if(f.address>=base&&f.address-base<=UINT32_MAX)f.rva=static_cast<uint32_t>(f.address-base);
 for(size_t i=0;i<std::size(native::kBlocks);++i){const auto&b=native::kBlocks[i];if(f.address>=base+b.rva&&f.address-(base+b.rva)<b.size){f.block=static_cast<uint32_t>(i);break;}}
 f.samples=static_cast<unsigned>((std::min)(size_t{16},a.captured-first));
 for(unsigned i=0;i<f.samples;++i){f.actual[i]=a.data[first+i];if(expected(f.address+i,f.expected[i]))f.known|=static_cast<uint16_t>(1u<<i);}
 return f;
}
inline bool Contracts(const Reader&r,uintptr_t base,size_t count,std::span<const OwnPatch>patches,Failure&f){
 f={};if(count>2||count!=patches.size()){f.stage=Stage::NativeContracts;f.detail=Detail::ShapeOrOpaque;return false;}
 Audit a(r);auto reader=a.Wrapped();const auto expected=[&](uintptr_t at,uint8_t&v){return Expected(base,at,patches,v);};
 if(!CheckContracts(reader,base,native::kBlocks,patches)){f=Describe(Stage::NativeContracts,a,base,expected);return false;}
 a.seen=false;if(!native::ValidateBindings(reader,base)){f=Describe(Stage::NativeBindings,a,base,expected);return false;}
 a.seen=false;if(!superjump::boss::ValidateProfile(reader,base).valid){f=Describe(Stage::BossProfile,a,base,expected);return false;}return true;
}
struct ExecAudit {ExecFn fn=nullptr;void*context=nullptr;uintptr_t at=0;size_t size=0;bool failed=false;
 static bool Check(void*p,uintptr_t at,size_t n){auto&e=*static_cast<ExecAudit*>(p);const bool ok=e.fn&&e.fn(e.context,at,n);if(!ok){e.failed=true;e.at=at;e.size=n;}return ok;}
};
struct BridgeSpec {uintptr_t site=0,callback=0,stub=0,trampoline=0;std::array<uint8_t,5>original{},patch{};};
inline bool BridgeExpected(const BridgeSpec&s,uintptr_t at,uint8_t&v){
 if(at>=s.site&&at-s.site<5){v=s.patch[at-s.site];return true;}
 if(at>=s.trampoline&&at-s.trampoline<5){v=s.original[at-s.trampoline];return true;}
 if(at==s.trampoline+5){v=0xe9;return true;}
 if(at>=s.trampoline+6&&at-s.trampoline<10){const uint32_t back=static_cast<uint32_t>(s.site+5-(s.trampoline+10));v=static_cast<uint8_t>(back>>((at-s.trampoline-6)*8));return true;}
 if(at==s.trampoline+10){v=0xff;return true;}if(at==s.trampoline+11){v=0x25;return true;}
 if(at>=s.trampoline+12&&at-s.trampoline<16){v=0;return true;}
 if(at>=s.stub&&at-s.stub<std::size(kMidStubPrefix)){v=kMidStubPrefix[at-s.stub];return true;}
 for(auto[field,value]:{std::pair<uintptr_t,uintptr_t>{s.stub+375,s.callback},{s.stub+383,s.trampoline},{s.trampoline+16,s.stub}})if(at>=field&&at-field<8){v=static_cast<uint8_t>(value>>((at-field)*8));return true;}
 return false;
}
inline bool Bridge(const Reader&r,uintptr_t base,unsigned index,const BridgeSpec&s,ExecFn exec,void*context,Failure&f){
 Audit a(r);ExecAudit e{exec,context};BridgeProof proof{};
 const bool valid=ValidateBridge(a.Wrapped(),s.site,s.original.data(),s.callback,ExecAudit::Check,&e,proof);
 if(valid&&proof.stub==s.stub&&proof.trampoline==s.trampoline)return true;
 f=Describe(Stage::Bridge,a,base,[&](uintptr_t at,uint8_t&v){return BridgeExpected(s,at,v);});f.site=index;f.expectedStub=s.stub;f.expectedTrampoline=s.trampoline;f.actualStub=proof.stub;f.actualTrampoline=proof.trampoline;
 if(valid)f.detail=Detail::Ownership;if(e.failed){f.detail=Detail::PrivateExecutable;f.execAddress=e.at;f.execSize=e.size;}return false;
}
}
