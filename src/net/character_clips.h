// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "animation/anim_clip.h"
#include "animation/anim_task.h"
#include "characters/anim_set.h"
#include "human/human_animator.h"
#include "net/client_world.h"

// Where a client finds the clips a snapshot names for a human of one character: the character's clips by anim id and
// the gait blends its locomotion plays. Coney's own; the original has no network play
// (docs/guides/client-world.md#clips).

namespace coney::net {

/// A ClipSource over one character's clips (characters::AnimSet: its own data, then the generic defaults), with the
/// gait blends of the locomotion's two move styles built from `slots` as human::HumanAnimator::gaitBlend() builds them:
/// the walk, jog, run, sprint and sprint slots, and the hidden style's, whose walk is 633 `STEALTH_WALK`
/// (human::kAnimStealthWalk). A clip one gait blend names is in that blend; the jog, run and sprint are in both, and
/// are answered with the blend of the move style asked for, which a snapshot carries for each human
/// (HumanState::moveStyle). A style's own walk answers its own blend whichever style is asked, so a layer a style
/// change is fading out keeps its blend.
/// @coney-only network play; the original has none
class CharacterClips final : public ClipSource {
  public:
    /// The clips of `anims` (which must outlive this), with the gait blends of `slots`.
    explicit CharacterClips(const characters::AnimSet& anims,
                            const human::AnimSlots& slots = human::AnimSlots::player());

    /// The clip of anim id `animId` in the set; null for an id it has no clip for, or one out of range.
    [[nodiscard]] const anim::AnimClip* clip(std::int32_t animId) const override;
    /// The five clips of the gait blend `animId` is one of in `style`: that style's blend for the jog, run or sprint
    /// (the normal style's walk, jog, run, sprint and sprint; the hidden style's with 633 for the walk), and for a
    /// style's own walk (408 or 633) its blend whichever style is asked; nothing for any other id, or when the set
    /// lacks one of the blend's clips.
    [[nodiscard]] std::optional<std::array<anim::GaitClip, 5>> gaitClips(std::int32_t animId,
                                                                         MoveStyle style) const override;

  private:
    // The blend of `ids` (walk, jog, run, sprint, sprint), or nothing when a clip is missing.
    [[nodiscard]] std::optional<std::array<anim::GaitClip, 5>> blendOf(const std::array<std::uint32_t, 5>& ids) const;

    const characters::AnimSet& m_anims;
    std::array<std::uint32_t, 5> m_normal{}; // the normal style's walk, jog, run, sprint, sprint
    std::array<std::uint32_t, 5> m_hidden{}; // the hidden style's: its own walk, the rest the normal style's
};

} // namespace coney::net
