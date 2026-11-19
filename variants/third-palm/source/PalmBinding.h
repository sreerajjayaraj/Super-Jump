#pragma once
#include "PalmNativeProfiles.h"
#include "PalmTrueDoubleJump107Profiles.generated.h"
#include "native-trace/TracePolicy.h"
#include <array>
#include <cstring>
namespace superjump::palm::binding {
using Reader=motiontrace::Reader;using Snapshot=motiontrace::Snapshot;using Slot=motiontrace::Slot;
enum class Reason{Snapshot,NoProfile,Slot,Metadata,Package,Record,Clip,Changed,Valid};
struct Result{bool valid=false;Action action=Action::Unknown;Reason reason=Reason::Snapshot;uint32_t hash=0;};
inline bool Span(uintptr_t at,size_t size){return size&&at>=0x10000&&at<0x800000000000ULL&&size<=0x800000000000ULL-at;}
inline bool Equals(const Reader&r,uintptr_t at,const uint8_t*expected,size_t size){if(!expected||!Span(at,size))return false;std::array<uint8_t,256>actual{};for(size_t i=0;i<size;){const auto n=(size-i)<actual.size()?size-i:actual.size();if(!r.Bytes(at+i,actual.data(),n)||memcmp(actual.data(),expected+i,n))return false;i+=n;}return true;}
struct Ownership{uintptr_t package=0,metadata_array=0,records=0,strings=0,offsets=0;uint16_t metadata_count=0;uint32_t record_count=0,string_count=0;Slot slot{};std::array<uintptr_t,2>record{},header{},record_package{};std::array<uint32_t,2>string_offset{};bool operator==(const Ownership&)const=default;};
inline bool Probe(const Reader&r,const Slot&captured,const native::Profile&p,Ownership&out,Reason&reason){
 reason=Reason::Slot;if(!captured.present||!captured.effective||captured.suppressed||!motiontrace::ReadSlot(r,captured.pointer,out.slot)||out.slot!=captured)return false;
 out.package=captured.second;reason=Reason::Package;if(!Span(out.package,0x2c0)||!r.Get(out.package+0x238,out.metadata_array)||!r.Get(out.package+0x2b0,out.metadata_count)||out.metadata_count!=p.metadata_count||p.index>=out.metadata_count||!Span(out.metadata_array,size_t(out.metadata_count)*288)||captured.metadata!=out.metadata_array+size_t(p.index)*288||!r.Get(out.package+0x248,out.records)||!r.Get(out.package+0x250,out.record_count)||out.record_count!=p.record_count||!Span(out.records,size_t(out.record_count)*8)||!r.Get(out.package+0x38,out.strings)||!r.Get(out.package+0x40,out.offsets)||!r.Get(out.package+0x48,out.string_count)||out.string_count!=p.animation_count||!Span(out.offsets,size_t(out.string_count)*4)||out.strings<0x10000)return false;
 reason=Reason::Metadata;if(!Equals(r,captured.metadata,p.metadata,288)||p.variants.empty()||p.variants.size()>2)return false;
 unsigned i=0;for(const auto&v:p.variants){reason=Reason::Record;if(v.index>=out.record_count||!v.bytes||v.size<0xa8||v.size>65536||!r.Get(out.records+size_t(v.index)*8,out.record[i])||!Span(out.record[i],0x40)||!r.Get(out.record[i]+0x10,out.record_package[i])||out.record_package[i]!=out.package||!r.Get(out.record[i]+0x28,out.header[i])||!Equals(r,out.header[i],v.bytes,v.size))return false;
  reason=Reason::Clip;uint16_t clip=0;if(!r.Get(out.header[i]+0x9e,clip)||clip!=v.clip||clip>=out.string_count||!r.Get(out.offsets+size_t(clip)*4,out.string_offset[i])||out.string_offset[i]>0x1000000||!v.name||!v.name_size||v.name_size>512||!Span(out.strings,size_t(out.string_offset[i])+v.name_size)||!Equals(r,out.strings+out.string_offset[i],reinterpret_cast<const uint8_t*>(v.name),v.name_size))return false;++i;
 }
 Slot after{};reason=Reason::Changed;return motiontrace::ReadSlot(r,captured.pointer,after)&&after==captured;
}
inline Result Classify(const Reader&r,const Snapshot&s){
 Result result{};if(!s.valid||s.hero!=motiontrace::Hero::Kliff||s.hero_key!=1)return result;result.reason=Reason::NoProfile;
 const auto match=[&](const auto& profiles){
  for(const auto& p:profiles){
   const auto& slot=p.lane==native::Lane::Upper?s.upper:s.lower;
   if(!slot.present||!slot.effective||slot.suppressed||slot.skill_key!=p.key||slot.name_hash!=p.hash)continue;
   Ownership first{},second{};
   if(!Probe(r,slot,p,first,result.reason)||!Probe(r,slot,p,second,result.reason))return false;
   if(first!=second){result.reason=Reason::Changed;return false;}
   result={true,p.action,Reason::Valid,p.hash};return true;
  }
  return false;
 };
 if(match(native::kProfiles))return result;
 // Only a stable record mismatch can select the exact replacement record.
 // An unreadable owner, altered metadata or a torn sample still refuses.
 if(result.reason==Reason::Record)match(native::kTrueDoubleJump107OpeningProfiles);
 return result;
}
}
