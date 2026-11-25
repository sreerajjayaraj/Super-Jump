#pragma once
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>

namespace superjump::motion {
inline constexpr uint8_t kPrelude[] = {0x83,0x7C,0x24,0x30,0x02,0x0F,0x28,0x75,0x90};
inline constexpr std::array<uint8_t,5> kEntry{0x74,0x3E,0x0F,0x28,0xC6};
inline constexpr std::array<uint8_t,7> kBranchTarget{0x48,0x8B,0x8D,0xD0,0x03,0x00,0x00};
inline constexpr size_t kBranchTargetOffset = 0x40;
inline constexpr size_t kWitnessBytes = sizeof(kPrelude) + kBranchTargetOffset + kBranchTarget.size();

template<class Reader,class MatchOwner>
uintptr_t UniqueOwner(const Reader& reader,uintptr_t base,size_t ownerSize,MatchOwner matches) {
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    if (!base || !reader.Get(base, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew < static_cast<LONG>(sizeof(dos)) || dos.e_lfanew > 0x1000 ||
        !reader.Get(base + dos.e_lfanew, nt) || nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXCEPTION) return 0;

    const auto imageBytes = nt.OptionalHeader.SizeOfImage;
    const auto& owners = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if (imageBytes < 0x1000 || imageBytes > 0x30000000 || imageBytes > UINTPTR_MAX - base ||
        !owners.VirtualAddress || owners.VirtualAddress >= imageBytes ||
        !owners.Size || owners.Size > imageBytes - owners.VirtualAddress ||
        owners.Size % sizeof(RUNTIME_FUNCTION)) return 0;

    std::array<RUNTIME_FUNCTION,256> rows{};
    uintptr_t found = 0;
    for (size_t offset = 0; offset < owners.Size;) {
        const auto bytes = (std::min)(sizeof(rows), size_t(owners.Size) - offset);
        if (!reader.Bytes(base + owners.VirtualAddress + offset, rows.data(), bytes)) return 0;
        for (size_t index = 0; index < bytes / sizeof(RUNTIME_FUNCTION); ++index) {
            const auto& row = rows[index];
            if (row.EndAddress <= row.BeginAddress || row.EndAddress > imageBytes ||
                row.EndAddress - row.BeginAddress != ownerSize ||
                !matches(base+row.BeginAddress)) continue;
            if (found != 0) return 0;
            found = base+row.BeginAddress;
        }
        offset += bytes;
    }
    return found;
}

template<class Reader, class OwnsCode>
uintptr_t LocateProducer(const Reader& reader, uintptr_t base, OwnsCode ownsCode) {
    // The motion owner and both branch paths are stable when their RVAs move.
    constexpr size_t ownerSize = 0xBE8;
    constexpr size_t preludeOffset = 0x6D3;
    const auto owner=UniqueOwner(reader,base,ownerSize,[&](uintptr_t address) {
        const auto prelude=address+preludeOffset;
        std::array<uint8_t,kWitnessBytes> code{};
        return ownsCode(prelude,kWitnessBytes) && reader.Bytes(prelude,code.data(),code.size()) &&
               std::equal(std::begin(kPrelude),std::end(kPrelude),code.begin()) &&
               std::equal(kEntry.begin(),kEntry.end(),code.begin()+sizeof(kPrelude)) &&
               std::equal(kBranchTarget.begin(),kBranchTarget.end(),
                          code.begin()+sizeof(kPrelude)+kBranchTargetOffset);
    });
    return owner?owner+preludeOffset+sizeof(kPrelude):0;
}
}
