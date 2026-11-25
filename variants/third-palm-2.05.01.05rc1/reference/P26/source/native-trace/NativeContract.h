#pragma once
#include "TracePolicy.h"
#include <span>
#include <algorithm>
namespace motiontrace {
struct ContractBlock{uint32_t rva;const uint8_t*bytes;size_t size;bool allow_own_patch;};
struct OwnPatch{uint32_t rva=0;std::array<uint8_t,32>bytes{};size_t size=0;};
inline constexpr uint32_t kOriginalCollectorSites[]={0x98E5B1,0x98EDFF};
inline bool CheckContracts(const Reader&r,uintptr_t base,std::span<const ContractBlock>blocks,
    std::span<const OwnPatch>patches,std::span<const uint32_t> resolvedSites=kOriginalCollectorSites){
 if(!base||blocks.empty()||patches.size()>2)return false;
 for(size_t i=0;i<patches.size();++i){const auto&p=patches[i];if((std::find(resolvedSites.begin(),resolvedSites.end(),p.rva)==resolvedSites.end())||p.size!=5||p.bytes[0]!=0xe9)return false;for(size_t j=0;j<i;++j)if(p.rva==patches[j].rva)return false;}
 std::array<uint8_t,256> actual{};
 for(const auto&b:blocks){if(!b.bytes||!b.size||b.rva>=0x144E9000||b.size>0x144E9000-b.rva)return false;
  for(size_t o=0;o<b.size;){const size_t n=(b.size-o)<actual.size()?b.size-o:actual.size();if(!r.Bytes(base+b.rva+o,actual.data(),n))return false;
   for(size_t j=0;j<n;++j){uint8_t expected=b.bytes[o+j];if(b.allow_own_patch)for(const auto&p:patches){const auto at=uintptr_t(b.rva)+o+j;if(at>=p.rva&&at-p.rva<p.size)expected=p.bytes[at-p.rva];}if(actual[j]!=expected)return false;}o+=n;
  }
 }return true;
}
inline bool HostMayStart(const wchar_t*name,bool identity){if(!identity||!name)return false;constexpr wchar_t wanted[]=L"CrimsonDesert.exe";for(size_t i=0;i<std::size(wanted);++i){auto c=name[i],v=wanted[i];if(c>=L'A'&&c<=L'Z')c+=L'a'-L'A';if(v>=L'A'&&v<=L'Z')v+=L'a'-L'A';if(c!=v)return false;if(!c)return true;}return false;}
}
