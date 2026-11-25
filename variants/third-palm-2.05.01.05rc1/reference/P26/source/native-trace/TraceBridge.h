#pragma once
#include "NativeContract.h"
#include "MidStubContract.h"
namespace motiontrace {
using ExecFn=bool(*)(void*,uintptr_t,size_t);
struct BridgeProof{uintptr_t relay=0,trampoline=0,stub=0;};
inline bool RelativeTarget(uintptr_t start,size_t length,int32_t displacement,uintptr_t&out){if(start>=0x800000000000ULL||length>0x800000000000ULL-start)return false;const int64_t result=static_cast<int64_t>(start+length)+displacement;if(result<0x10000||result>=0x800000000000LL)return false;out=static_cast<uintptr_t>(result);return true;}
inline bool ValidateBridge(const Reader&r,uintptr_t site,const uint8_t*original,uintptr_t callback,ExecFn executable,void*execContext,BridgeProof&proof){
 proof={};if(!original||!executable||!callback)return false;uint8_t e9=0;int32_t disp=0;uintptr_t relay=0;
 if(!r.Get(site,e9)||e9!=0xe9||!r.Get(site+1,disp)||!RelativeTarget(site,5,disp,relay)||relay<0x1000A)return false;
 const uintptr_t trampoline=relay-10;if(!executable(execContext,trampoline,24))return false;
 std::array<uint8_t,5>copy{};uint8_t resume=0;int32_t back=0;uintptr_t destination=0;
 if(!r.Bytes(trampoline,copy.data(),copy.size())||std::memcmp(copy.data(),original,copy.size())||!r.Get(trampoline+5,resume)||resume!=0xe9||!r.Get(trampoline+6,back)||!RelativeTarget(trampoline+5,5,back,destination)||destination!=site+5)return false;
 uint16_t ff25=0;uint32_t indirect=0;uintptr_t stub=0;
 if(!r.Get(relay,ff25)||ff25!=0x25ff||!r.Get(relay+2,indirect)||indirect||!r.Get(relay+6,stub)||!executable(execContext,stub,391))return false;
 std::array<uint8_t,std::size(kMidStubPrefix)>prefix{};uintptr_t actualCallback=0,actualTrampoline=0;
 if(!r.Bytes(stub,prefix.data(),prefix.size())||std::memcmp(prefix.data(),kMidStubPrefix,prefix.size())||!r.Get(stub+375,actualCallback)||actualCallback!=callback||!r.Get(stub+383,actualTrampoline)||actualTrampoline!=trampoline)return false;
 proof={relay,trampoline,stub};return true;
}
}
