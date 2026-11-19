#include "PalmTraceFixture.h"
#include "../source/PalmNativeProfiles.h"
#include "../source/PalmTrueDoubleJump107Profiles.generated.h"
#ifndef PALM_BIND_BASELINE
#include "../source/PalmBinding.h"
#endif
using namespace superjump::palm;
struct BindingFixture:Fixture {
 const superjump::palm::native::Profile&profile;uintptr_t package,meta_array,metadata,records,strings,offsets;std::array<uintptr_t,2>record{},header{},name{};uintptr_t slot;
 explicit BindingFixture(unsigned index):BindingFixture(superjump::palm::native::kProfiles[index]){}
 explicit BindingFixture(const superjump::palm::native::Profile& input):profile(input){
  package=m.Alloc(0x2c0);meta_array=m.Alloc(size_t(profile.metadata_count)*288);metadata=meta_array+size_t(profile.index)*288;Copy(metadata,profile.metadata,288);m.Put(package+0x238,meta_array);m.Put(package+0x2b0,uint16_t(profile.metadata_count));records=m.Alloc(size_t(profile.record_count)*8);m.Put(package+0x248,records);m.Put(package+0x250,profile.record_count);strings=m.Alloc(2048);offsets=m.Alloc(size_t(profile.animation_count)*4);m.Put(package+0x38,strings);m.Put(package+0x40,offsets);m.Put(package+0x48,profile.animation_count);size_t at=0;unsigned j=0;
  for(const auto&v:profile.variants){record[j]=m.Alloc(0x40);header[j]=m.Alloc(v.size);Copy(header[j],v.bytes,v.size);m.Put(record[j]+0x10,package);m.Put(record[j]+0x28,header[j]);m.Put(records+size_t(v.index)*8,record[j]);name[j]=strings+at;Copy(name[j],v.name,v.name_size);m.Put(offsets+size_t(v.clip)*4,uint32_t(at));at+=v.name_size;++j;}
  slot=profile.lane==superjump::palm::native::Lane::Upper?upper:lower;if(profile.lane==superjump::palm::native::Lane::Lower)m.Put(umeta+0x111,uint8_t(0));m.Put(slot+8,package);m.Put(slot+0x10,metadata);
 }
 void Copy(uintptr_t at,const void*data,size_t n){if(at<0x10000||at-0x10000>m.bytes.size()||n>m.bytes.size()-(at-0x10000))throw 1;memcpy(m.bytes.data()+at-0x10000,data,n);}
};
int main(){unsigned checks=0,failed=0;auto ck=[&](bool x,const char*n){++checks;if(!x){++failed;printf("FAIL %s\n",n);}};
#ifdef PALM_BIND_BASELINE
 BindingFixture f(11);f.m.Put(f.metadata+0x54,uint32_t(1));auto s=f.Read();ck(!(s.upper.present&&s.upper.effective&&s.upper.skill_key==15035&&s.upper.name_hash==0x1690D50E),"generic native-slot tuple must not authorize a changed record/phase binding");
#else
 for(unsigned i=0;i<std::size(superjump::palm::native::kProfiles);++i){BindingFixture f(i);auto s=f.Read();auto b=binding::Classify({&f.m,Memory::Read},s);ck(s.valid&&b.valid&&b.action==f.profile.action,"exact current profile binds effective metadata and full serialized action/clip owner");}
 for(const auto& opener:superjump::palm::native::kTrueDoubleJump107OpeningProfiles){
  BindingFixture f(opener);auto s=f.Read();const auto b=binding::Classify({&f.m,Memory::Read},s);
  ck(s.valid&&b.valid&&b.action==Action::InitialJump,"known composed True Double-Jump 1.0.7 opening record qualifies");
  f.m.Put(f.header[0]+0xa8,uint8_t(opener.variants[0].bytes[0xa8]^0x80));
  ck(!binding::Classify({&f.m,Memory::Read},s).valid,"a further change to a supported modded opening record still refuses");
 }
 for(unsigned mutation=0;mutation<17;++mutation){BindingFixture f(11);auto s=f.Read();if(mutation==0)f.m.Put(f.metadata+0x54,uint32_t(1));if(mutation==1)f.m.Put(f.metadata+0xee,uint16_t(2));if(mutation==2)f.m.Put(f.metadata+0x20,uint8_t(77));if(mutation==3)f.m.Put(f.package+0x238,f.meta_array+288);if(mutation==4)f.m.Put(f.package+0x2b0,uint16_t(1));if(mutation==5)f.m.Put(f.package+0x250,uint32_t(1));if(mutation==6)f.m.Put(f.record[0]+0x10,f.package+16);if(mutation==7)f.m.Put(f.record[0]+0x28,f.header[0]+16);if(mutation==8)f.m.Put(f.header[0]+0x9e,uint16_t(1));if(mutation==9)f.m.Put(f.header[0]+0xa8,uint8_t(93));if(mutation==10)f.m.Put(f.name[0],uint8_t('X'));if(mutation==11)f.m.Put(f.package+0x48,uint32_t(1));if(mutation==12)f.m.Put(f.slot+0x118,uintptr_t(0));if(mutation==13)f.m.Put(f.slot+8,f.package+16);if(mutation==14)f.m.Put(f.records+size_t(f.profile.variants[0].index)*8,uintptr_t(0));if(mutation==15)f.m.Put(f.metadata+0x111,uint8_t(0));if(mutation==16)f.m.Put(f.metadata+0xa8,uint32_t(15033));ck(!binding::Classify({&f.m,Memory::Read},s).valid,"metadata/full-record/clip/owner/active mutation invalidates rather than retaining authority");}
 {BindingFixture f(11);auto s=f.Read();s.valid=false;f.m.reads=0;ck(!binding::Classify({&f.m,Memory::Read},s).valid&&f.m.reads==0,"invalid typed snapshot cannot authorize any profile or read ownership tree");}
 {BindingFixture f(11);auto s=f.Read();s.hero_key=1001367;ck(!binding::Classify({&f.m,Memory::Read},s).valid,"clone actor cannot bind main Kliff native trigger");}
 {BindingFixture f(0);f.m.Put(f.upper+0x10,f.metadata);f.m.Put(f.upper+8,f.package);auto s=f.Read();ck(s.lower.suppressed&&!binding::Classify({&f.m,Memory::Read},s).valid,"retained ineffective lower jump-start cannot arm behind effective upper2");}
 {BindingFixture f(11);auto s=f.Read();f.m.reads=0;auto good=binding::Classify({&f.m,Memory::Read},s);ck(good.valid,"stable binder fixture before read failure sweep");auto n=f.m.reads;for(size_t i=1;i<=n;++i){BindingFixture q(11);auto snapshot=q.Read();q.m.reads=0;q.m.fail=i;ck(!binding::Classify({&q.m,Memory::Read},snapshot).valid,"mandatory binding read failure is unproved");}}
 {BindingFixture f(11);auto s=f.Read();unsigned seen=0;f.m.mutation=[&](uintptr_t at){if(at==f.package+0x238&&++seen==2)f.m.Put(at,f.meta_array+288);};ck(!binding::Classify({&f.m,Memory::Read},s).valid,"package metadata owner change between probes cannot preserve earlier binding");}
#endif
 printf("Native palm binding checks=%u failures=%u\n",checks,failed);return failed?1:0;}
