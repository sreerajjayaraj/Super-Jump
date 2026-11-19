#pragma once
#include "SuperJumpGameDoubleJumpPolicy.h"
namespace superjump {
struct PendingActivation {
 GameDoubleJumpState game{};bool armed=false,pending=false;uint32_t pendingAt=0,lastPress=0;
};
inline void CancelPendingActivation(PendingActivation&s){
 s.game.hasFirstImpulse=false;s.game.firstImpulseMs=0;s.game.impulseArmed=false;
 s.armed=false;s.pending=false;s.pendingAt=0;s.lastPress=0;
 // Preserve existing cooldown; restriction exit cannot bypass it.
}
}
