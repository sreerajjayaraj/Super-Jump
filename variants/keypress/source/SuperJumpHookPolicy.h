#pragma once

namespace superjump {

enum class DeltaInstallDecision {
    KeepWaiting,
    InstallDirect,
    InstallChain,
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

} // namespace superjump
