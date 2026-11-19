#include <cassert>
#include <cstdint>

#include "../source/SuperJumpInputPolicy.h"

int main() {
    using superjump::KeyboardKeypressDecision;

    uint16_t buttons = 0;
    uint8_t connectedMask = 0;

    superjump::AccumulateXInputSlot(true, 0, 0, buttons, connectedMask);
    superjump::AccumulateXInputSlot(true, 0x4000, 1, buttons, connectedMask);
    superjump::AccumulateXInputSlot(false, 0x1000, 2, buttons, connectedMask);

    assert(buttons == 0x4000);
    assert(connectedMask == 0x3);

    // A provider cached before Steam Input/controller fallback may report no
    // slots, while the provider used by Freedom Flyer reports the controller.
    // Merging providers must preserve the later valid jump edge.
    uint16_t providerButtons = 0;
    uint8_t providerMask = 0;
    superjump::AccumulateXInputProvider(
        0x0, 0x0, providerButtons, providerMask);
    superjump::AccumulateXInputProvider(
        0x4000, 0x1, providerButtons, providerMask);
    assert(providerButtons == 0x4000);
    assert(providerMask == 0x1);

    // Buttons exposed through different providers are combined rather than
    // allowing the first loaded DLL to become a permanent single source.
    superjump::AccumulateXInputProvider(
        0x40, 0x2, providerButtons, providerMask);
    assert(providerButtons == 0x4040);
    assert(providerMask == 0x3);

    assert(superjump::ShouldEvaluateChordTrigger(0));
    assert(!superjump::ShouldEvaluateChordTrigger(1));
    assert(!superjump::ShouldEvaluateChordTrigger(2));

    assert(superjump::EvaluateKeyboardKeypress(
               0, true, false, true, false, false) ==
           KeyboardKeypressDecision::Trigger);
    assert(superjump::EvaluateKeyboardKeypress(
               0, true, true, true, false, false) ==
           KeyboardKeypressDecision::None);
    assert(superjump::EvaluateKeyboardKeypress(
               0, true, false, false, false, false) ==
           KeyboardKeypressDecision::None);
    assert(superjump::EvaluateKeyboardKeypress(
               0, true, false, true, true, false) ==
           KeyboardKeypressDecision::SuppressedUi);
    assert(superjump::EvaluateKeyboardKeypress(
               0, true, false, true, false, true) ==
           KeyboardKeypressDecision::SuppressedSession);
    assert(superjump::EvaluateKeyboardKeypress(
               1, true, false, true, false, false) ==
           KeyboardKeypressDecision::None);
    return 0;
}
