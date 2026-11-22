// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/character_clips.h"

#include <algorithm>
#include <cstddef>

#include "characters/character_data.h"

namespace coney::net {

CharacterClips::CharacterClips(const characters::AnimSet& anims, const human::AnimSlots& slots) : m_anims(anims) {
    // As HumanAnimator::gaitBlend() builds its blend: walk, jog, run, sprint and the sprint again; the hidden style
    // changes only the walk (HumanAnimator::setStealthStyle()).
    const std::uint32_t sprint = slots.ids[human::kSlotSprint];
    m_normal = {slots.ids[human::kSlotWalk], slots.ids[human::kSlotJog], slots.ids[human::kSlotRun], sprint, sprint};
    m_hidden = m_normal;
    m_hidden[0] = human::kAnimStealthWalk;
}

const anim::AnimClip* CharacterClips::clip(std::int32_t animId) const {
    if (animId < 0 || static_cast<std::size_t>(animId) >= characters::kAnimIds) {
        return nullptr;
    }
    return m_anims.clip(static_cast<std::size_t>(animId));
}

std::optional<std::array<anim::GaitClip, 5>> CharacterClips::gaitClips(std::int32_t animId, MoveStyle /*style*/) const {
    if (animId < 0) {
        return std::nullopt;
    }
    const auto id = static_cast<std::uint32_t>(animId);
    // The normal style first: its jog, run and sprint are the hidden style's too.
    if (std::ranges::find(m_normal, id) != m_normal.end()) {
        return blendOf(m_normal);
    }
    if (id == m_hidden[0]) {
        return blendOf(m_hidden);
    }
    return std::nullopt;
}

std::optional<std::array<anim::GaitClip, 5>> CharacterClips::blendOf(const std::array<std::uint32_t, 5>& ids) const {
    std::array<anim::GaitClip, 5> blend{};
    for (std::size_t i = 0; i < ids.size(); ++i) {
        const anim::AnimClip* found = clip(static_cast<std::int32_t>(ids.at(i)));
        // A GaitBlendTask needs all five clips.
        if (found == nullptr) {
            return std::nullopt;
        }
        blend.at(i) = anim::GaitClip{.clip = found, .animId = ids.at(i)};
    }
    return blend;
}

} // namespace coney::net
