#pragma once

#include "SiteResolver.h"

#include <cstdint>
#include <optional>

namespace superjump {

struct PalmPublication {
    std::uintptr_t nativeState;
    unsigned collectorSite;
    bool accepted;
};

// At the publisher entry, RCX is the former RSI and R9D is the zero-extended
// result from AL. The return RVA identifies the original collector call site.
inline std::optional<PalmPublication> ClassifyPalmPublication(
    const NativeSites& sites, std::uintptr_t imageBase, std::uintptr_t returnAddress,
    std::uintptr_t receiver, std::uint32_t acceptedArgument) {
    if (!receiver || !imageBase || returnAddress <= imageBase) return std::nullopt;
    const auto returnRva = returnAddress - imageBase;
    for (unsigned i = 0; i < sites.palmReturnSites.size(); ++i) {
        if (returnRva == sites.palmReturnSites[i])
            return PalmPublication{receiver, i, acceptedArgument == 1};
    }
    return std::nullopt;
}

} // namespace superjump
