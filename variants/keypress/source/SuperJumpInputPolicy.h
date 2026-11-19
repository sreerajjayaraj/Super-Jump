#pragma once

#include <cstddef>
#include <cstdint>

namespace superjump {

constexpr bool ShouldEvaluateChordTrigger(const int triggerMode) {
    return triggerMode == 0;
}

enum class KeyboardKeypressDecision {
    None,
    Trigger,
    SuppressedUi,
    SuppressedSession
};

constexpr KeyboardKeypressDecision EvaluateKeyboardKeypress(
    const int triggerMode,
    const bool keyDown,
    const bool previousKeyDown,
    const bool modifiersDown,
    const bool interactionUiVisible,
    const bool interactionSessionBlocked) {
    if (!ShouldEvaluateChordTrigger(triggerMode) ||
        !keyDown || previousKeyDown || !modifiersDown) {
        return KeyboardKeypressDecision::None;
    }
    if (interactionUiVisible) {
        return KeyboardKeypressDecision::SuppressedUi;
    }
    if (interactionSessionBlocked) {
        return KeyboardKeypressDecision::SuppressedSession;
    }
    return KeyboardKeypressDecision::Trigger;
}

constexpr const char* PreferredXInputProviderName(const std::size_t index) {
    switch (index) {
    case 0: return "xinput1_4.dll";
    case 1: return "xinput1_3.dll";
    case 2: return "xinput9_1_0.dll";
    default: return nullptr;
    }
}

constexpr int SelectPreferredXInputProvider(
    const uint8_t* connectedMasks,
    const std::size_t count) {
    if (!connectedMasks) return -1;
    for (std::size_t index = 0; index < count; ++index) {
        if (connectedMasks[index] != 0) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

constexpr void AccumulateXInputSlot(
    bool connected,
    uint16_t slotButtons,
    uint8_t slotIndex,
    uint16_t& combinedButtons,
    uint8_t& connectedMask) {
    if (!connected || slotIndex >= 8) {
        return;
    }
    combinedButtons = static_cast<uint16_t>(combinedButtons | slotButtons);
    connectedMask = static_cast<uint8_t>(connectedMask | (1u << slotIndex));
}

constexpr void AccumulateXInputProvider(
    uint16_t providerButtons,
    uint8_t providerConnectedMask,
    uint16_t& combinedButtons,
    uint8_t& combinedConnectedMask) {
    if (providerConnectedMask == 0) {
        return;
    }
    combinedButtons = static_cast<uint16_t>(
        combinedButtons | providerButtons);
    combinedConnectedMask = static_cast<uint8_t>(
        combinedConnectedMask | providerConnectedMask);
}

} // namespace superjump
