#include "../src/LayoutValidation.h"
#include "../src/SiteResolver.h"

#include <Windows.h>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <vector>

namespace {
struct FileHandle {
    HANDLE value{INVALID_HANDLE_VALUE};
    ~FileHandle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};

struct ImageView {
    const std::uint8_t* data{};
    ~ImageView() { if (data) UnmapViewOfFile(data); }
};

struct FileImageReader {
    const std::uint8_t* data;
    std::size_t size;
    std::uintptr_t preferredBase;
    std::uintptr_t changedEntry{};
    std::uintptr_t changedWriter{};
    std::uintptr_t changedStepper{};

    static bool Read(void* opaque, std::uintptr_t at, void* out, std::size_t size) noexcept {
        const auto& self = *static_cast<const FileImageReader*>(opaque);
        if (at < self.preferredBase || at - self.preferredBase >= self.size ||
            size > self.size - (at - self.preferredBase)) return false;
        __try {
            std::memcpy(out, self.data + (at - self.preferredBase), size);
            // Model a foreign modification without mapping, writing or
            // executing any game instructions.
            for (const auto changed : {self.changedEntry, self.changedWriter, self.changedStepper})
                if (changed >= at && changed - at < size)
                    static_cast<std::uint8_t*>(out)[changed - at] ^= 1;
            return true;
        } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER :
                    EXCEPTION_CONTINUE_SEARCH) {
            return false;
        }
    }
};
} // namespace

int wmain(int argc, wchar_t** argv) {
    using namespace superjump;
    if (argc != 2) return 2;
    const FileHandle file{CreateFileW(argv[1], GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr)};
    if (file.value == INVALID_HANDLE_VALUE) return 2;
    const FileHandle mapping{CreateFileMappingW(file.value, nullptr, PAGE_READONLY | SEC_IMAGE_NO_EXECUTE, 0, 0, nullptr)};
    if (!mapping.value) return 2;
    const ImageView image{static_cast<const std::uint8_t*>(MapViewOfFile(mapping.value, FILE_MAP_READ, 0, 0, 0))};
    if (!image.data) return 2;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image.data);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < static_cast<LONG>(sizeof(*dos)) ||
        dos->e_lfanew > 0x1000) return 2;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(image.data + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt->FileHeader.SizeOfOptionalHeader != sizeof(nt->OptionalHeader)) return 2;

    const auto imageSize = nt->OptionalHeader.SizeOfImage;
    auto source = FileImageReader{image.data, imageSize, nt->OptionalHeader.ImageBase};
    const boss::Reader reader{&source, FileImageReader::Read};
    std::vector<CodeSection> sections;
    const auto* section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if (!(section->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        const auto size = (std::min)(section->Misc.VirtualSize, section->SizeOfRawData);
        if (section->VirtualAddress >= imageSize || size > imageSize - section->VirtualAddress) return 2;
        sections.push_back({section->VirtualAddress, {image.data + section->VirtualAddress, size}});
    }
    const auto& exceptions = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if (exceptions.VirtualAddress >= imageSize || exceptions.Size > imageSize - exceptions.VirtualAddress ||
        exceptions.Size % sizeof(RUNTIME_FUNCTION)) return 2;
    const auto* rows = reinterpret_cast<const RUNTIME_FUNCTION*>(image.data + exceptions.VirtualAddress);
    std::vector<CodeOwner> owners;
    for (std::size_t i = 0; i < exceptions.Size / sizeof(RUNTIME_FUNCTION); ++i)
        owners.push_back({rows[i].BeginAddress, rows[i].EndAddress});

    const auto sites = ResolveNativeSites(sections, owners);
    if (sites.error != ResolveError::None) return 1;
    const auto profile = boss::ValidateProfile(reader, source.preferredBase);
    if (!profile.valid) {
        std::cerr << "Boss and actor profile rejected the mapped game image.\n";
        return 1;
    }
    const auto layout = ValidateGameLayout(reader, profile, {}, sites.sites.palm);
    if (layout != LayoutResult::Valid) {
        std::cerr << "Layout validation failed: " << static_cast<unsigned>(layout) << '\n';
        return 1;
    }
    std::cout << "Startup layout, movement and action-binding witnesses accepted in the read-only game image.\n";
    source.changedEntry = source.preferredBase + (profile.shifted_layout ? 0x4606D60 : menu60::kLocomotion);
    if (ResolveMovementPath(reader, profile, {}, true) != MovementPath::NativeContinuation ||
        ValidateGameLayout(reader, profile, {}, sites.sites.palm) != LayoutResult::Valid) {
        std::cerr << "A foreign entry change prevented validation of the independent native path.\n";
        return 1;
    }
    source.changedWriter = source.preferredBase + (profile.shifted_layout ? 0x4607B2F : menu60::kDeltaRva) + 8;
    if (ResolveMovementPath(reader, profile, {}, true) != MovementPath::Unavailable) return 1;
    source.changedWriter = 0;
    source.changedStepper = source.preferredBase + menu60::ShiftedStepper(profile.shifted_layout);
    if (ResolveMovementPath(reader, profile, {}, true) != MovementPath::Unavailable) return 1;
    std::cout << "Local native path accepts a separate movement entry and refuses altered writer or stepper.\n";
}
