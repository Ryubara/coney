// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "ai/action.h"

// The taunt action (`Taunt`, type 29): the human says a line and plays a taunt of one of eight kinds (anim `0x253` +
// the kind: the advancing taunts, the cheers, the taunts...), and with most kinds the action lasts while it plays.
// The shared event handler queues it for the men gloating over one just put on the ground.
// Research: docs/research/ai-reimpl.md#taunt-action, docs/research/gang-fights.md#waiting

namespace coney::ai {

/// The taunt kinds' first anim (kind 0); a kind of kNoClipTaunt or more plays nothing.
inline constexpr int kTauntFirstAnim = 0x253;
inline constexpr int kNoClipTaunt = 8;
/// The kinds the gloating uses: the cheers (anim `0x256`) and the taunts (`0x257`).
inline constexpr int kCheerTaunt = 3;
inline constexpr int kTauntAtTaunt = 4;
/// The class role (`CharClass` `+0x11b`) whose taunt plays even when he is busy.
inline constexpr int kAlwaysTauntRole = 13;
/// The taunt's blend in, seconds.
inline constexpr float kTauntFade = 0.05F;

/// The anim of taunt kind `kind` (kTauntFirstAnim + kind), as the anim-state builder maps a taunt's kind.
/// @orig 0x0025f028 Human_BuildAnimState (unknown)
[[nodiscard]] constexpr int tauntAnim(int kind) { return kTauntFirstAnim + kind; }

/// The taunt action (vtable `0x00542e60`).
/// @orig 0x002fb0f0 TauntAction_Init (unknown)
class TauntAction final : public Action {
  public:
    /// A taunt of `kind` saying speech command `line` (0 none), after `delayMs` (kRandomDelay: 0-500 ms).
    TauntAction(int kind, int line, std::int16_t delayMs = 0) : Action(delayMs), m_kind(kind), m_line(line) {}

    /// Done at once for a cop or a class of role 6. Below kNoClipTaunt the clip: done when he is not free to fidget
    /// (FidgetAction's test) unless his class's role is 13; else anim tauntAnim(kind), the gang's clip for it at a
    /// random variant (Gang::randomClip()) or the character's own. Then the line, and it waits for the clip for kinds
    /// 3-7 (the others are done on their next update). **Coney stand-ins**: the clip holds kScriptedClipHeld rather
    /// than the taunt flags `0x100000` / `0x200000`; it is a scripted clip, not the original's overlay, so the human's
    /// move is stopped while it plays; the line is said at one volume.
    /// @orig 0x002fb128 TauntAction_Start (unknown)
    [[nodiscard]] ActionStatus start(Brain& brain) override;
    /// Done at once without the wait, else once the clip holds none of its record flags; either way a normal end.
    /// @orig 0x002fb2f0 TauntAction_Update (unknown)
    [[nodiscard]] ActionStatus update(Brain& brain) override;
    /// After a normal end (update() done; the original's mode 0) accepts and leaves the clip playing. Otherwise
    /// stops the taunt (`Human_StopTaunt`: the idle again while its clip still plays) and refuses when the taunt waits
    /// for its clip and the clip still holds its flags (`0x300000` in the original, kScriptedClipHeld's in Coney).
    /// **Coney reading**: Coney has no abort mode, so a normal end is the action's own update() having ended it,
    /// which is the only way Brain::finishAction() reaches it; the original's count at `0x005112ac` (read by nothing)
    /// is not kept.
    /// @orig 0x002fb278 TauntAction_Abort (unknown)
    [[nodiscard]] bool abort(Brain& brain) override;

    /// The kind and the line.
    [[nodiscard]] int kind() const { return m_kind; }
    [[nodiscard]] int line() const { return m_line; }

  private:
    int m_kind;                    // +0x0c
    int m_line;                    // +0x18
    bool m_wait = true;            // +0x14
    std::uint32_t m_heldFlags = 0; // the record flags the clip holds; 0 when none plays
    bool m_ended = false;          // update() ended it: a normal end
    int m_playing = -1;            // the anim it plays
};

} // namespace coney::ai
