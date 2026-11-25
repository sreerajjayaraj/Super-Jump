#include "GameImage.h"

#include <Psapi.h>
#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>

namespace superjump {

bool ReadGameMemory(void*, std::uintptr_t address, void* output, std::size_t size) noexcept {
    constexpr std::uintptr_t limit = 0x800000000000ULL;
    if (address < 0x10000 || address >= limit || !size || size > limit - address || !output) return false;
    __try {
        std::memcpy(output, reinterpret_cast<const void*>(address), size);
        return true;
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ||
                GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR ? EXCEPTION_EXECUTE_HANDLER :
                EXCEPTION_CONTINUE_SEARCH) {
        return false;
    }
}

bool GameRegion(std::uintptr_t base, std::uintptr_t address, std::size_t size, bool executable) noexcept {
    if (!size || size > UINTPTR_MAX - address) return false;
    const auto end = address + size;
    while (address < end) {
        MEMORY_BASIC_INFORMATION region{};
        if (VirtualQuery(reinterpret_cast<const void*>(address), &region, sizeof(region)) != sizeof(region) ||
            reinterpret_cast<std::uintptr_t>(region.AllocationBase) != base ||
            region.State != MEM_COMMIT || region.Type != MEM_IMAGE ||
            (region.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
        const auto protection = region.Protect & 0xff;
        const bool exec = protection == PAGE_EXECUTE_READ || protection == PAGE_EXECUTE_READWRITE ||
                          protection == PAGE_EXECUTE_WRITECOPY;
        const bool read = protection == PAGE_READONLY || protection == PAGE_READWRITE ||
                          protection == PAGE_WRITECOPY;
        if (!exec && (executable || !read)) return false;
        const auto start = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
        if (start > address || region.RegionSize > UINTPTR_MAX - start ||
            start + region.RegionSize <= address) return false;
        address = start + region.RegionSize;
    }
    return true;
}

bool Witness::Matches() const noexcept {
    std::array<std::uint8_t, 48> current{};
    return length && length <= bytes.size() && ReadGameMemory(nullptr, address, current.data(), length) &&
        std::equal(bytes.begin(), bytes.begin() + length, current.begin());
}

bool LoadedSites::DependenciesMatch() const noexcept {
    return std::ranges::all_of(dependencies, &Witness::Matches) &&
        (!sites.HasJumpCommit() || std::ranges::all_of(jumpCommitDependencies, &Witness::Matches));
}

GameResolution ResolveGameImage(HMODULE module) {
    MODULEINFO info{};
    if (!module || !K32GetModuleInformation(GetCurrentProcess(), module, &info, sizeof(info)) ||
        info.lpBaseOfDll != module) return {};
    const auto base = reinterpret_cast<std::uintptr_t>(module);
    const auto size = info.SizeOfImage;
    if (!size || size > UINTPTR_MAX - base) return {};
    const auto read = [&](std::size_t offset, void* to, std::size_t count, bool executable) {
        return offset < size && count <= size - offset && GameRegion(base, base + offset, count, executable) &&
               ReadGameMemory(nullptr, base + offset, to, count);
    };
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    if (!read(0, &dos, sizeof(dos), false) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew < static_cast<LONG>(sizeof(dos)) ||
        !read(dos.e_lfanew, &nt, sizeof(nt), false) || nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.FileHeader.SizeOfOptionalHeader != sizeof(nt.OptionalHeader) ||
        nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXCEPTION) return {};

    std::vector<std::vector<std::uint8_t>> snapshots;
    std::vector<CodeSection> code;
    for (unsigned i = 0; i < nt.FileHeader.NumberOfSections; ++i) {
        IMAGE_SECTION_HEADER section{};
        const auto offset = std::size_t(dos.e_lfanew) + sizeof(nt) + i * sizeof(section);
        if (!read(offset, &section, sizeof(section), false)) return {};
        const auto length = (std::min)(section.Misc.VirtualSize, section.SizeOfRawData);
        if (!(section.Characteristics & IMAGE_SCN_MEM_EXECUTE) || !length) continue;
        if (section.VirtualAddress >= size || length > size - section.VirtualAddress) return {};
        auto& copy = snapshots.emplace_back(length);
        if (!read(section.VirtualAddress, copy.data(), copy.size(), true)) return {};
        code.push_back({section.VirtualAddress, copy});
    }
    std::ranges::sort(code, {}, &CodeSection::rva);
    const auto& dir = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if (!dir.VirtualAddress || dir.VirtualAddress >= size || !dir.Size ||
        dir.Size > size - dir.VirtualAddress || dir.Size % sizeof(RUNTIME_FUNCTION)) return {};
    std::vector<RUNTIME_FUNCTION> rows(dir.Size / sizeof(RUNTIME_FUNCTION));
    if (!read(dir.VirtualAddress, rows.data(), dir.Size, false)) return {};
    std::vector<CodeOwner> owners;
    owners.reserve(rows.size());
    for (const auto& row : rows) {
        if (row.EndAddress > size) return {};
        owners.push_back({row.BeginAddress, row.EndAddress});
    }

    const auto result = ResolveNativeSites(code, owners);
    if (result.error != ResolveError::None) return {result.error, {}};
    LoadedSites live{};
    live.sites = result.sites;
    const auto capture = [&](Witness& span, std::uint32_t rva, std::size_t count) {
        for (const auto& section : code) {
            if (rva < section.rva) break;
            const auto offset = rva - section.rva;
            if (offset >= section.bytes.size() || count > section.bytes.size() - offset) continue;
            span.address = base + rva;
            span.length = count;
            std::ranges::copy(section.bytes.subspan(offset, count), span.bytes.begin());
            return true;
        }
        return false;
    };
    const auto [motion, palm, publisher, returnSites, jumpCommit] = result.sites;
    constexpr std::uint32_t callPrefix = 12, callSpan = 43;
    if (!capture(live.original[0], motion, 5) || !capture(live.original[1], publisher, 7)) return {};
    const auto displacement = std::bit_cast<std::int8_t>(live.original[0].bytes[1]);
    const auto taken = static_cast<std::uint32_t>(std::int64_t(motion) + 2 + displacement);
    if (!capture(live.dependencies[0], motion - 9, 9) ||
        !capture(live.dependencies[1], taken, 13) ||
        !capture(live.dependencies[2], palm[0] - callPrefix, callSpan) ||
        !capture(live.dependencies[3], palm[1] - callPrefix, callSpan) ||
        !capture(live.dependencies[4], publisher + 7, 8)) return {};
    if (result.sites.HasJumpCommit()) {
        for (std::size_t i = 0; i < jumpCommit.size(); ++i) {
            const auto& span = jumpCommit[i];
            if (!capture(live.jumpCommitDependencies[i], span.rva, span.length)) return {};
        }
    }
    return {ResolveError::None, live};
}

} // namespace superjump
