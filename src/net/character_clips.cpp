// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/character_clips.h"

namespace coney::net {

CharacterClips::CharacterClips(const characters::AnimSet& anims, const human::AnimSlots& slots) : m_anims(anims) {
    (void)slots;
}

const anim::AnimClip* CharacterClips::clip(std::int32_t /*animId*/) const { return nullptr; }

std::optional<std::array<anim::GaitClip, 5>> CharacterClips::gaitClips(std::int32_t /*animId*/) const {
    return std::nullopt;
}

std::optional<std::array<anim::GaitClip, 5>>
CharacterClips::blendOf(const std::array<std::uint32_t, 5>& /*ids*/) const {
    return std::nullopt;
}

} // namespace coney::net
