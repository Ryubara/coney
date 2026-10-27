// SPDX-License-Identifier: GPL-3.0-or-later
// A human's clips in a revive: the reviver's 351, 353 and the way back to his idle, and the revived human's heal and
// 354. Who may revive whom, the flash and the lines are the game mode's (gamemodes/gameplay_revive.cpp).
// Research: docs/research/combat-reactions.md#revive
#include <array>
#include <cstdint>
#include <span>

#include "combat/revive.h"
#include "human/fighter_clips.h"
#include "human/human.h"

namespace coney::human {

void Human::startRevive(const Combatant& downed) {
    // The led steer puts him in place: turned to the downed human's head and slid to 351's reach from it, over 351's
    // time to its first event + 0.1 s, at an even rate (about 0.70 m away and 12 degrees off the line at runtime).
    m_velocity = anim::Vec3{};
    m_fighter.steerRevive(downed, m_position, m_heading, m_animator);
    // 351 then 353, then his idle loop: through 389 out of the fight stance (idle set 0), the fight idle in it (set
    // 0xb), all after a 0.01 s blend and holding 0x8000000.
    constexpr std::array<std::uint32_t, 3> kOutOfStance{combat::kReviveBeginAnim, combat::kReviveEndAnim,
                                                        clips::kNormalFromFight};
    constexpr std::array<std::uint32_t, 2> kInStance{combat::kReviveBeginAnim, combat::kReviveEndAnim};
    const bool stance = m_fighter.inFightStance();
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
    // Up with 354 as the pair's clip from the reviver's set, holding 0x8000, then the loop of the reviver's idle set:
    // out of the fight stance through 389 into the idle; with the reviver in the stance straight into the fight idle
    // (set 0xb), no 389. **Coney stand-in** (#1011): 389 comes from the reviver's set with 354, where the original
    // chains it on the revived man's own stack from his own set.
    m_velocity = anim::Vec3{};
    constexpr std::array<std::uint32_t, 2> kUpOutOfStance{combat::kRevivedAnim, clips::kNormalFromFight};
    constexpr std::array<std::uint32_t, 1> kUpInStance{combat::kRevivedAnim};
    const bool stance = reviver.m_fighter.inFightStance();
    const std::span<const std::uint32_t> chain =
        stance ? std::span<const std::uint32_t>(kUpInStance) : std::span<const std::uint32_t>(kUpOutOfStance);
    m_animator.playPaired(chain, reviver.animator().anims(), stance ? kAnimFightIdle : clips::kIdle, AnimState::Attack,
                          0.0F, HeldFlags{.held = combat::kRevivedHeld, .set = combat::kRevivedHeld});
}

} // namespace coney::human
