#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>

#include "../source/SuperJumpInputPolicy.h"

int main() {
    assert(std::strcmp(superjump::PreferredXInputProviderName(0),
                       "xinput1_4.dll") == 0);
    assert(std::strcmp(superjump::PreferredXInputProviderName(1),
                       "xinput1_3.dll") == 0);
    assert(std::strcmp(superjump::PreferredXInputProviderName(2),
                       "xinput9_1_0.dll") == 0);
    assert(superjump::PreferredXInputProviderName(3) == nullptr);

    const uint8_t firstProviderConnected[] = {0x1, 0x0, 0x0};
    assert(superjump::SelectPreferredXInputProvider(
               firstProviderConnected,
               std::size(firstProviderConnected)) == 0);

    const uint8_t fallbackProviderConnected[] = {0x0, 0x2, 0x1};
    assert(superjump::SelectPreferredXInputProvider(
               fallbackProviderConnected,
               std::size(fallbackProviderConnected)) == 1);

    const uint8_t noProviderConnected[] = {0x0, 0x0, 0x0};
    assert(superjump::SelectPreferredXInputProvider(
               noProviderConnected,
               std::size(noProviderConnected)) == -1);

    return 0;
}
