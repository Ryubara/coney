// SPDX-License-Identifier: GPL-3.0-or-later
// A human's clips in a revive: the reviver's 351, 353 and the way back to his idle, and the revived human's heal and
// 354. Who may revive whom, the flash and the lines are the game mode's (gamemodes/gameplay_revive.cpp).
// Research: docs/research/combat.md#revive
#include <array>
#include <cstdint>
#include <span>

#include "combat/revive.h"
#include "human/fighter_clips.h"
#include "human/human.h"

namespace coney::human {

void Human::startRevive(anim::Vec3 downed) {
    // The led steer puts him in place over 0.1 s. **Coney's reading**: he turns to the downed human and stays where
    // he stands; whether the Anim Range List gives 351 a reach to slide to is not on the page.
    turnToFace(downed, combat::kReviveSteerSeconds);
    m_velocity = anim::Vec3{};
    // 351 then 353, then his idle loop: through 389 out of the fight stance (idle set 0), the fight idle in it (set
    // 0xb), all after a 0.01 s blend and holding 0x8000000.
    constexpr std::array<std::uint32_t, 3> kOutOfStance{combat::kReviveBeginAnim, combat::kReviveEndAnim,
                                                        clips::kNormalFromFight};
    constexpr std::array<std::uint32_t, 2> kInStance{combat::kReviveBeginAnim, combat::kReviveEndAnim};
    const bool stance = m_fighter.lockTarget() != nullptr;
    const std::span<const std::uint32_t> chain =
        stance ? std::span<const std::uint32_t>(kInStance) : std::span<const std::uint32_t>(kOutOfStance);
    m_animator.playCombat(chain, stance ? kAnimFightIdle : clips::kIdle, AnimState::Attack, combat::kReviveFadeSeconds,
                          HeldFlags{.held = combat::kReviverHeld, .set = combat::kReviverHeld});
}

bool Human::reviveBeginPlaying() const { return m_animator.animId() == combat::kReviveBeginAnim; }

bool Human::reviveEndPlaying() const { return m_animator.animId() == combat::kReviveEndAnim; }

void Human::playRevived(const Human& reviver) {
    // Healed in full (`Human_Heal` with "full"): wounding over, up from the ground with all his health.
    setWounded(false);
    revive();
    // Up with 354 as the pair's clip from the reviver's set, then his idle. **Coney's reading**: no 389 before the
    // idle (he wakes out of the fight stance) and the 0x8000 the pair task holds is not kept.
    m_velocity = anim::Vec3{};
    m_animator.playPaired(clips::one(combat::kRevivedAnim), reviver.animator().anims(), clips::kIdle,
                          AnimState::Attack);
}

} // namespace coney::human
