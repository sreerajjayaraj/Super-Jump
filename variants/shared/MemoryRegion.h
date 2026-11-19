#pragma once
#include <Windows.h>
#include <cstdint>

namespace native_memory {
inline bool Region(uintptr_t at,size_t n,uintptr_t owner,DWORD type,DWORD protection){
 MEMORY_BASIC_INFORMATION m{};return VirtualQuery(reinterpret_cast<void*>(at),&m,sizeof(m))==sizeof(m)&&m.State==MEM_COMMIT&&m.Type==type&&m.Protect==protection&&reinterpret_cast<uintptr_t>(m.AllocationBase)==owner&&at>=reinterpret_cast<uintptr_t>(m.BaseAddress)&&at-reinterpret_cast<uintptr_t>(m.BaseAddress)<=m.RegionSize&&n<=m.RegionSize-(at-reinterpret_cast<uintptr_t>(m.BaseAddress));
}
}
