// SPDX-License-Identifier: GPL-3.0-or-later
// The two entries an AI action stops a fidget or a taunt through: each names the anim ids its own action plays, which
// the AI's headers hold, and hands them to Human::stopOverlay() (the stop itself, human.cpp).
// Research: docs/research/ai-reimpl.md#fidget-action, docs/research/ai-reimpl.md#taunt-action
#include "human/human.h"

#include "ai/fidget_action.h"
#include "ai/taunt_action.h"

namespace coney::human {

// @coney-only the entry the fidget action calls; the stop itself, with Human_StopFidget's tag, is stopOverlay()
void Human::stopFidget(std::uint32_t held) {
    // The clips a standing stop ends are the fight and idle fidgets (0x25b, 0x25c).
    stopOverlay(held, static_cast<std::uint32_t>(ai::kFightFidgetAnim),
                static_cast<std::uint32_t>(ai::kIdleFidgetAnim));
}

// @coney-only the entry the taunt action calls; the stop itself, with Human_StopTaunt's tag, is stopOverlay()
void Human::stopTaunt(std::uint32_t held) {
    // The taunts' clips are 0x253 to 0x25a (the knife fidget's, the last kind's, among them).
    stopOverlay(held, static_cast<std::uint32_t>(ai::kTauntFirstAnim),
                static_cast<std::uint32_t>(ai::tauntAnim(ai::kNoClipTaunt - 1)));
}

} // namespace coney::human
