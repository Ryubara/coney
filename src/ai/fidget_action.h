// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "ai/action.h"

// The fidget action (`PlayFidget`, type 15): the human says a line and plays a fidget clip by anim id, and the action
// lasts while the clip plays. The fight goal's waiting attackers queue it as their taunt (anim `0x25b`, the combat
// fidgets, which the gang's clip table fills while it fights).
// Research: docs/research/ai-reimpl.md#fidget-action, docs/research/ai.md#fight-reposition

namespace coney::ai {

/// The fidget anims: a knife holder's (`0x25a`), the fight fidgets (`0x25b`, the gang's group kCombatFidgetClips) and
/// the idle fidgets (`0x25c`).
inline constexpr int kKnifeFidgetAnim = 0x25a;
inline constexpr int kFightFidgetAnim = 0x25b;
inline constexpr int kIdleFidgetAnim = 0x25c;
/// The class of a dog (human `+0xcc`) and the class role (`CharClass` `+0x11b`) that never fidget.
inline constexpr int kDogClass = 0xdd;
inline constexpr int kNoFidgetRole = 6;
/// The anim set of a held knife (Fighter::animSet()).
inline constexpr int kKnifeAnimSet = 1;
/// The fidget's blend in, seconds (`Human_PushScriptOverlay`'s 0.1 s overlay).
inline constexpr float kFidgetFade = 0.1F;

/// Whether `brain`'s human may start a fidget or a taunt (`Human_IsFreeForFidget`). **Coney stand-in**: no held record
/// flag, alive, up, not held and empty-handed (the state-flag word `0x7bf9e9f7ff0` is not built).
/// @orig 0x00225f58 Human_IsFreeForFidget (unknown)
[[nodiscard]] bool freeToFidget(const Brain& brain);
/// `brain`'s class role (`CharClass` `+0x11b`) through his gang's lookups; nothing without a gang or a lookup.
/// @coney-only lookup glue; the original reads the class record's byte directly
[[nodiscard]] std::optional<int> classRole(const Brain& brain);

/// The fidget action (vtable `0x00542c60`).
/// @orig 0x002f9f48 FidgetAction_Init (unknown)
class FidgetAction final : public Action {
  public:
    /// A fidget on anim `animId` (with `idle`, the idle fidgets instead), saying speech command `line` (0 none), after
    /// `delayMs`.
    FidgetAction(int animId, bool idle, int line, std::int16_t delayMs = 0)
        : Action(delayMs), m_animId(animId), m_idle(idle), m_line(line) {}

    /// Done at once for a cop, a dog or a class of role 6, and when he is not free to fidget (a knife in hand excused
    /// without `idle`); otherwise says the line, and (unless a dealer without `idle`) plays the clip
    /// (`Human_PlayHeldIdle`): the idle fidgets with `idle`, else `0x25a` with a knife in hand, else the anim, which
    /// for `0x25b` is the gang's clip at a random variant (Gang::randomClip()) or else the character's own clip. Done
    /// when nothing plays.
    /// **Coney stand-ins**: "free to fidget" is freeToFidget()'s reading; the line is said at one volume (the
    /// original's 1.5 × at a player's man is not built); the clip is a scripted clip holding kScriptedClipHeld, not an
    /// overlay holding `0x100`.
    /// @orig 0x002f9f78 FidgetAction_Start (unknown)
    [[nodiscard]] ActionStatus start(Brain& brain) override;
    /// Done once the clip holds none of the record flags it started with.
    /// @orig 0x002fa1e0 FidgetAction_Update (unknown)
    [[nodiscard]] ActionStatus update(Brain& brain) override;
    /// Stops the fidget (`Human_StopFidget`: the idle again while its clip still plays) and accepts. **Coney
    /// stand-in**: the refusal for an idle Warrior whose chief stands with his crew at rest is not built (no caller
    /// queues an idle fidget yet).
    /// @orig 0x002fa0e0 FidgetAction_Abort (unknown)
    [[nodiscard]] bool abort(Brain& brain) override;

    /// The anim, whether it is an idle fidget, and its line.
    [[nodiscard]] int animId() const { return m_animId; }
    [[nodiscard]] bool idle() const { return m_idle; }
    [[nodiscard]] int line() const { return m_line; }

  private:
    int m_animId;                  // +0x0c
    bool m_idle;                   // +0x10
    int m_line;                    // +0x14
    std::uint32_t m_heldFlags = 0; // the record flags the clip holds; 0 when none plays
    int m_playing = -1;            // the anim id it plays
};

} // namespace coney::ai
