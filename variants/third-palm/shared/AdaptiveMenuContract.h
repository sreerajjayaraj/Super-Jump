
#pragma once

// Included after movement, writer and executable-region contracts have been declared.
namespace adaptive {
template<size_t N,size_t M>
struct Pattern {
    std::array<uint8_t,N> bytes;
    std::array<uint16_t,M> displacements;

    bool Matches(const uint8_t* actual) const {
        size_t pos=0;
        for (const auto at:displacements) {
            if (std::memcmp(bytes.data()+pos,actual+pos,at-pos)) return false;
            pos=at+4;
        }
        return !std::memcmp(bytes.data()+pos,actual+pos,N-pos);
    }
};
#include "AdaptiveMenuPatterns.generated.h"

struct Image {
    Reader reader;
    uintptr_t owner;
    IMAGE_NT_HEADERS64 nt{};
    uintptr_t textRva=0;
    std::vector<uint8_t> text;

    bool Contains(uintptr_t rva,size_t count) const {
        return rva && rva<=nt.OptionalHeader.SizeOfImage &&
               count<=nt.OptionalHeader.SizeOfImage-rva;
    }

    bool Load() {
        IMAGE_DOS_HEADER dos{};
        if (!reader.Get(owner,dos) || dos.e_magic!=IMAGE_DOS_SIGNATURE ||
            dos.e_lfanew<64 || dos.e_lfanew>0x1000 || !reader.Get(owner+dos.e_lfanew,nt) ||
            nt.Signature!=IMAGE_NT_SIGNATURE || nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64 ||
            nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            nt.FileHeader.SizeOfOptionalHeader!=sizeof(IMAGE_OPTIONAL_HEADER64) ||
            nt.OptionalHeader.SizeOfImage>0x4000000 || !nt.FileHeader.NumberOfSections ||
            nt.FileHeader.NumberOfSections>32) return false;
        auto at=owner+dos.e_lfanew+sizeof(nt);
        for (unsigned i=0;i<nt.FileHeader.NumberOfSections;++i,at+=sizeof(IMAGE_SECTION_HEADER)) {
            IMAGE_SECTION_HEADER section{};
            if (!reader.Get(at,section)) return false;
            if (std::memcmp(section.Name,".text\0\0",8)) continue;
            if (textRva || !(section.Characteristics&IMAGE_SCN_MEM_EXECUTE) ||
                section.Misc.VirtualSize>0x1000000 ||
                !Contains(section.VirtualAddress,section.Misc.VirtualSize)) return false;
            textRva=section.VirtualAddress;
            text.resize(section.Misc.VirtualSize);
            if (!reader.Bytes(owner+textRva,text.data(),text.size())) return false;
        }
        return textRva && !text.empty();
    }

    template<size_t N,size_t M,class Filter>
    uintptr_t Find(const Pattern<N,M>& pattern,Filter filter) const {
        const size_t prefix=M?pattern.displacements.front():N;
        if (!prefix || N>text.size()) return 0;
        uintptr_t found=0;
        const auto last=text.end()-N+prefix;
        for (auto pos=text.begin(); pos<last;) {
            pos=std::search(pos,last,pattern.bytes.begin(),pattern.bytes.begin()+prefix);
            if (pos==last) break;
            const auto rva=textRva+static_cast<uintptr_t>(pos-text.begin());
            if (pattern.Matches(&*pos) && filter(rva)) {
                if (found) return 0;
                found=rva;
            }
            ++pos;
        }
        return found;
    }

    template<size_t N,size_t M>
    uintptr_t Find(const Pattern<N,M>& pattern) const {
        return Find(pattern,[](uintptr_t){return true;});
    }

    uintptr_t Relative(uintptr_t at,size_t displacement,size_t length) const {
        int32_t rel=0;
        if (at<textRva || at-textRva>text.size() || length>text.size()-(at-textRva) ||
            displacement+sizeof(rel)>length) return 0;
        std::memcpy(&rel,text.data()+(at-textRva)+displacement,sizeof(rel));
        const auto target=static_cast<uintptr_t>(static_cast<int64_t>(at+length)+rel);
        return Contains(target,1)?target:0;
    }

    template<size_t N>
    bool CopyCode(uintptr_t rva,std::array<uint8_t,N>& output) const {
        if (rva<textRva || rva-textRva>text.size() || N>text.size()-(rva-textRva)) return false;
        std::memcpy(output.data(),text.data()+(rva-textRva),N);
        return true;
    }

    bool FunctionRow(uintptr_t start,uintptr_t& address,std::array<uint32_t,3>& row) const {
        const auto directory=nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
        if (!Contains(directory.VirtualAddress,directory.Size) || directory.Size%sizeof(row)) return false;
        size_t low=0,high=directory.Size/sizeof(row);
        while (low<high) {
            const auto middle=low+(high-low)/2;
            const auto offset=directory.VirtualAddress+middle*sizeof(row);
            if (!reader.Get(owner+offset,row)) return false;
            if (row[0]==start) {
                address=offset;
                return row[1]>row[0] && Contains(row[0],row[1]-row[0]) && Contains(row[2],4);
            }
            if (row[0]<start) low=middle+1;
            else high=middle;
        }
        return false;
    }
};

inline ModeOwner DiscoverMode(const Image& image) {
    ModeOwner mode;
    const auto handler=image.Find(kMode);
    const auto install=handler?image.Find(kModeInstall,[&](uintptr_t at) {
        return image.Relative(at+0x1B,3,7)==handler;
    }):0;
    if (install) {
        mode.legacy.entry=handler;
        mode.legacy.original=image.Relative(handler+10,3,7);
        mode.legacy.target=image.Relative(install,3,7);
        if (!mode.legacy.original || !mode.legacy.target ||
            image.Relative(install+0x14,3,7)!=mode.legacy.original ||
            !image.FunctionRow(handler,mode.legacy.rowRva,mode.legacy.row) ||
            !image.CopyCode(handler,mode.legacyBytes))
            mode.legacy={};
    }
    const auto typed=image.Find(kTypedMode);
    const auto typedInstall=typed?image.Find(kTypedInstall,[&](uintptr_t at) {
        return image.Relative(at+0x1E,3,7)==typed;
    }):0;
    if (typedInstall) {
        mode.typed.entry=typed;
        mode.typed.target=image.Relative(typedInstall,3,7);
        mode.typed.original=image.Relative(typed+13,3,7);
        if (!mode.typed.target || !mode.typed.original ||
            image.Relative(typedInstall+0x14,3,7)!=mode.typed.original ||
            !image.FunctionRow(typed,mode.typed.rowRva,mode.typed.row) ||
            !image.CopyCode(typed,mode.typedBytes))
            mode.typed={};
    }
    return mode;
}

inline bool Discover(Reader reader,uintptr_t owner,Profile& result,AdaptiveLayout& layout) {
    Image image{reader,owner};
    if (!image.Load()) return false;
    const auto flight=image.Find(kFlightHelper), locomotion=image.Find(kLocomotion), stepper=image.Find(kStepper);
    const auto locInstall=image.Find(kLocomotionInstall), stepInstall=image.Find(kStepperInstall);
    const auto emit=image.Find(kWriterEmitter), publish=image.Find(kWriterPublish),
               shared=image.Find(kWriterShared), flightState=image.Find(kFlightState);
    if (!flight || !locomotion || !stepper || !locInstall || !stepInstall ||
        !emit || !publish || !shared || !flightState) return false;
    if (image.Relative(locomotion+0x5C,1,5)!=flight ||
        image.Relative(locInstall+7,3,7)!=locomotion ||
        image.Relative(stepInstall+7,3,7)!=stepper ||
        image.Relative(locInstall+17,1,5)!=image.Relative(stepInstall+17,1,5)) return false;

    Profile candidate{};
    candidate.flightHelper=flight;
    candidate.flightEnabled=image.Relative(flight,2,6);
    candidate.jumpGate=locomotion+0x45;
    candidate.jumpEnabled=image.Relative(candidate.jumpGate,2,6);
    candidate.locomotionHandler=locomotion;
    candidate.locomotionOriginal=image.Relative(locInstall,3,7);
    candidate.locomotionTarget=image.Relative(locInstall+0x1E,3,7);
    candidate.stepperHandler=stepper;
    candidate.stepperOriginal=image.Relative(stepInstall,3,7);
    candidate.stepperTarget=image.Relative(stepInstall+0x1E,3,7);
    candidate.movementReady=image.Relative(locInstall+0x68,2,6);
    if (!candidate.flightEnabled || !candidate.jumpEnabled || !candidate.locomotionOriginal ||
        !candidate.locomotionTarget || !candidate.stepperOriginal || !candidate.stepperTarget ||
        !candidate.movementReady || image.Relative(stepper+0x27,3,8)!=candidate.stepperOriginal ||
        image.Relative(flight+10,2,6)!=candidate.flightEnabled+4 ||
        image.Relative(flightState,2,6)!=candidate.flightEnabled+4 ||
        image.Relative(stepInstall+0x59,3,7)!=candidate.movementReady+1 ||
        !image.FunctionRow(stepper,candidate.stepperHandlerRow,candidate.stepperRow)) return false;

    AdaptiveLayout data{};
    constexpr std::array<uintptr_t,6> offsets{0x90,0xE9,0x142,0x1C2,0x263,0x2E7};
    for (size_t i=0;i<offsets.size();++i) {
        data.flight.slots[i]=image.Relative(emit+offsets[i],3,7);
        if (!data.flight.slots[i]) return false;
    }
    data.flight.patch=image.Relative(publish,4,8);
    data.flight.target=image.Relative(publish+0x18,3,7);
    data.flight.published=image.Relative(publish+0x52,3,7);
    data.flight.sharedBridge=image.Relative(shared,3,7);
    if (!data.flight.patch || image.Relative(publish+0x0D,3,7)!=data.flight.patch+8 ||
        data.flight.target!=data.flight.patch+0x10 ||
        data.flight.sharedBridge!=data.flight.target+8 || data.flight.published+8!=data.flight.patch ||
        image.Relative(flightState+6,4,8)!=data.flight.slots[2] ||
        image.Relative(flightState+0x28,5,9)!=data.flight.slots[3]) return false;

    data.jumpMultiply=locomotion+0x136;
    if (!image.CopyCode(flight,candidate.flightBytes) ||
        !image.CopyCode(candidate.jumpGate,candidate.jumpBytes) ||
        !image.CopyCode(locomotion,candidate.locomotionBytes) ||
        !image.CopyCode(stepper,candidate.stepperBytes) ||
        !image.CopyCode(data.jumpMultiply,data.jumpBytes)) return false;
    data.mode=DiscoverMode(image);
    layout=data;
    candidate.adaptive=&layout;
    result=candidate;
    return true;
}
} // namespace adaptive

