#pragma once

namespace superjump {

enum class DeltaInstallDecision {
    KeepWaiting,
    InstallDirect,
    InstallChain,
    FailClosed,
};

enum class PostYieldDeltaSiteDecision {
    ResolveFresh,
    UseCachedDirect,
    UseCachedChain,
    FailClosed,
};

constexpr DeltaInstallDecision DecideDeltaInstall(
    bool originalBytes,
    bool recognizedPriorHook,
    bool freedomFlyerSeen,
    bool requireFreedomFlyerChain,
    bool waitExpired) {
    if (recognizedPriorHook) {
        return DeltaInstallDecision::InstallChain;
    }
    if (!originalBytes) {
        return DeltaInstallDecision::FailClosed;
    }
    if (requireFreedomFlyerChain && freedomFlyerSeen) {
        return waitExpired ? DeltaInstallDecision::FailClosed
                           : DeltaInstallDecision::KeepWaiting;
    }
    return waitExpired ? DeltaInstallDecision::InstallDirect
                       : DeltaInstallDecision::KeepWaiting;
}

constexpr PostYieldDeltaSiteDecision SelectPostYieldDeltaSite(
    bool cachedSitePresent,
    bool cachedHasOriginalBytes,
    bool cachedHasRecognizedHook,
    bool requireFreedomFlyerChain,
    bool freedomFlyerLoaded) {
    if (!cachedSitePresent) {
        return PostYieldDeltaSiteDecision::ResolveFresh;
    }
    if (cachedHasRecognizedHook) {
        return PostYieldDeltaSiteDecision::UseCachedChain;
    }
    if (!cachedHasOriginalBytes) {
        return PostYieldDeltaSiteDecision::FailClosed;
    }
    if (requireFreedomFlyerChain && freedomFlyerLoaded) {
        return PostYieldDeltaSiteDecision::FailClosed;
    }
    return PostYieldDeltaSiteDecision::UseCachedDirect;
}

} // namespace superjump
