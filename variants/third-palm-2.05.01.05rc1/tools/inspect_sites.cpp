#include "../src/SiteResolver.h"
#include "../src/PalmPublishPolicy.h"

#include <Windows.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <vector>

namespace {
using namespace superjump;
using Byte = std::uint8_t;

template<class T>
bool ReadHeader(std::span<const Byte> file, std::size_t offset, T& header) {
    if (offset > file.size() || sizeof(T) > file.size() - offset) return false;
    std::memcpy(&header, file.data() + offset, sizeof(T));
    return true;
}

bool ParseImage(std::span<const Byte> file, std::vector<CodeSection>& code,
                std::vector<CodeOwner>& owners) {
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    if (!ReadHeader(file, 0, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew < sizeof(dos) || !ReadHeader(file, dos.e_lfanew, nt) ||
        nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt.FileHeader.SizeOfOptionalHeader != sizeof(nt.OptionalHeader) ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXCEPTION) return false;

    const auto& pdata = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if (!pdata.VirtualAddress || !pdata.Size || pdata.Size % sizeof(RUNTIME_FUNCTION)) return false;
    std::span<const Byte> unwind;
    for (unsigned i = 0; i < nt.FileHeader.NumberOfSections; ++i) {
        IMAGE_SECTION_HEADER section{};
        const std::size_t header = dos.e_lfanew + sizeof(nt) + i * sizeof(section);
        if (!ReadHeader(file, header, section)) return false;
        const auto raw = section.PointerToRawData;
        const auto size = (std::min)(section.SizeOfRawData, section.Misc.VirtualSize);
        if (raw > file.size() || size > file.size() - raw) return false;
        if ((section.Characteristics & IMAGE_SCN_MEM_EXECUTE) && size)
            code.push_back({section.VirtualAddress, file.subspan(raw, size)});
        if (pdata.VirtualAddress < section.VirtualAddress) continue;
        const auto offset = pdata.VirtualAddress - section.VirtualAddress;
        if (offset <= size && pdata.Size <= size - offset) {
            if (!unwind.empty()) return false;
            unwind = file.subspan(std::size_t(raw) + offset, pdata.Size);
        }
    }
    if (code.empty() || unwind.empty()) return false;
    std::ranges::sort(code, {}, &CodeSection::rva);
    for (std::size_t off = 0; off < unwind.size(); off += sizeof(RUNTIME_FUNCTION)) {
        RUNTIME_FUNCTION row{};
        if (!ReadHeader(unwind, off, row) || row.BeginAddress >= row.EndAddress ||
            row.EndAddress > nt.OptionalHeader.SizeOfImage) return false;
        owners.push_back({row.BeginAddress, row.EndAddress});
    }
    return true;
}

bool VerifyChanges(std::span<const CodeSection> code, const std::vector<CodeOwner>& owners,
                   NativeSites baseline) {
    std::vector<CodeOwner> resized = owners;
    for (auto& owner : resized) {
        if (owner.end <= baseline.motion || owner.begin > baseline.motion) continue;
        owner.end -= 1;
    }
    const auto accepted = ResolveNativeSites(code, resized);
    if (accepted.error != ResolveError::None || accepted.sites.motion != baseline.motion ||
        accepted.sites.palm != baseline.palm || accepted.sites.palmPublisher != baseline.palmPublisher ||
        accepted.sites.palmReturnSites != baseline.palmReturnSites) return false;

    std::vector<CodeSection> modified(code.begin(), code.end());
    for (auto& section : modified) {
        if (baseline.motion < section.rva || baseline.motion - section.rva >= section.bytes.size()) continue;
        std::vector<Byte> copy(section.bytes.begin(), section.bytes.end());
        const auto offset = baseline.motion - section.rva;
        copy[offset + 1] = 0;
        section.bytes = copy;
        if (ResolveNativeSites(modified, owners).error != ResolveError::MotionMissing) return false;
        section.bytes = code[&section - modified.data()].bytes;
        break;
    }
    for (auto& section : modified) {
        if (baseline.palm[1] < section.rva || baseline.palm[1] - section.rva >= section.bytes.size()) continue;
        std::vector<Byte> copy(section.bytes.begin(), section.bytes.end());
        copy[baseline.palm[1] - section.rva] = 0xCC;
        section.bytes = copy;
        return ResolveNativeSites(modified, owners).error == ResolveError::PalmMissing;
    }
    return false;
}

bool VerifyJumpCommit(std::span<const CodeSection> code, const std::vector<CodeOwner>& owners,
                      NativeSites baseline) {
    if (!baseline.HasJumpCommit()) return false;
    const auto result = baseline.jumpCommitWitnesses.back();
    std::vector<CodeSection> modified(code.begin(), code.end());
    for (auto& section : modified) {
        if (result.rva < section.rva || result.rva - section.rva >= section.bytes.size()) continue;
        std::vector<Byte> copy(section.bytes.begin(), section.bytes.end());
        copy[result.rva - section.rva] = 0xCC;
        section.bytes = copy;
        const auto altered = ResolveNativeSites(modified, owners);
        return altered.error == ResolveError::None && !altered.sites.HasJumpCommit() &&
            altered.sites.motion == baseline.motion && altered.sites.palm == baseline.palm &&
            altered.sites.palmPublisher == baseline.palmPublisher &&
            altered.sites.palmReturnSites == baseline.palmReturnSites;
    }
    return false;
}

bool VerifyPublisher(NativeSites sites) {
    constexpr std::uintptr_t base = 0x140000000;
    constexpr std::uintptr_t state = 0x141234560;
    const auto first = ClassifyPalmPublication(sites, base, base + sites.palmReturnSites[0], state, 1);
    const auto second = ClassifyPalmPublication(sites, base, base + sites.palmReturnSites[1], state, 0);
    return first && first->nativeState == state && first->collectorSite == 0 && first->accepted &&
           second && second->nativeState == state && second->collectorSite == 1 && !second->accepted &&
           !ClassifyPalmPublication(sites, base, base + sites.palmReturnSites[0] + 1, state, 1) &&
           !ClassifyPalmPublication(sites, base, base + sites.palmReturnSites[1], 0, 1);
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2 || argc > 3 || (argc == 3 && std::wstring_view(argv[2]) != L"--verify")) {
        std::cerr << "Usage: inspect-sites.exe <local game exe> [--verify]\n";
        return 2;
    }
    std::ifstream input(std::filesystem::path(argv[1]), std::ios::binary | std::ios::ate);
    const auto length = input.tellg();
    if (length <= 0 || length >= std::streamoff(1024) * 1024 * 1024) {
        std::cerr << "Invalid or unreadable image size.\n";
        return 2;
    }
    std::vector<Byte> image(static_cast<std::size_t>(length));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(image.data()), length)) return 2;
    std::vector<CodeSection> code;
    std::vector<CodeOwner> owners;
    if (!ParseImage(image, code, owners)) {
        std::cerr << "Invalid PE code sections or exception directory.\n";
        return 2;
    }
    const auto result = ResolveNativeSites(code, owners);
    if (result.error != ResolveError::None) {
        std::cerr << "Resolution refused: " << static_cast<unsigned>(result.error) << '\n';
        return 1;
    }
    std::cout << std::hex << "Motion RVA 0x" << result.sites.motion
        << "; collector RVAs 0x" << result.sites.palm[0] << " and 0x" << result.sites.palm[1] << '\n';
    std::cout << "Shared publisher RVA 0x" << result.sites.palmPublisher << "; return RVAs 0x"
        << result.sites.palmReturnSites[0] << " and 0x" << result.sites.palmReturnSites[1] << '\n';
    if (argc == 3) {
        if (!VerifyChanges(code, owners, result.sites) || !VerifyPublisher(result.sites) ||
            !VerifyJumpCommit(code, owners, result.sites)) return 1;
        std::cout << "Verified native jump consumer; altered commit disables only the extra Damiane route.\n";
        std::cout << "Passed: owner size variation; invalid motion branch; modified collector site; "
                     "publisher acceptance, rejection, unrelated caller, and missing receiver.\n";
    }
}
