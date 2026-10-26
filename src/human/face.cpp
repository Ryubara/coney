// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/face.h"

#include <algorithm>

#include "characters/anim_set.h"

// Stubs for the tests to compile against before the face is ported: they change nothing and return defaults.

namespace coney::human {

void FaceState::setMood(FaceMood /*mood*/) {}

void FaceState::beginTalk() {}

void FaceState::endTalk() {}

void FaceLayer::update(const FaceState& /*state*/, const characters::AnimSet& /*anims*/, bool /*shown*/) {}

void FaceLayer::advanceLoop(Loop& /*loop*/, float /*seconds*/, const characters::AnimSet& /*anims*/,
                            const Draw& /*draw*/) {}

void FaceLayer::advance(float /*seconds*/, const characters::AnimSet& /*anims*/, const Draw& /*draw*/) {}

std::optional<std::size_t> FaceLayer::playing() const { return std::nullopt; }

std::array<anim::Quat, anim::kPoseBones> FaceLayer::defaults(std::span<const anim::Quat, anim::kPoseBones> reference,
                                                             const characters::AnimSet& /*anims*/) const {
    std::array<anim::Quat, anim::kPoseBones> out{};
    std::ranges::copy(reference, out.begin());
    return out;
}

} // namespace coney::human
