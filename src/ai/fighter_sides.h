// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "ai/brain.h"
#include "ai/fighter_side.h"
#include "ai/gangs.h"

// The gangs the debug menus' spawned fighters join, one family per side, built from the original's own gang
// relationships: a human joins a gang (`Gang_AddMember`), two humans are friends when their gangs are the same, of one
// kind or made friends (`Gang_AreFriends`), and enemies when one gang's enemy bit names the other (`Gang_IsEnemyOf`).
// Research: docs/research/ai-gangs.md#gang-record, docs/research/ai-reimpl.md#gang-is-enemy-of

namespace coney::ai {

/// The kind of the sandbox fighters' gang: the level scripts' combat gangs' kind (`GangCreate(19, ...)` in
/// `level99_combat.lua`, docs/research/ai.md#level99).
inline constexpr int kFighterGangKind = 19;
/// The kind of the neutral fighters' gangs: rumble's first side (`GangCreate(3, "Gang1")`, docs/research/rumble.md), a
/// fighting kind that is neither the Warriors', the police's nor the enemy fighters', so by kind it is nobody's friend
/// and by its masks nobody's enemy.
inline constexpr int kNeutralGangKind = 3;

/// The gangs of the spawned fighters of each side, made as they are needed and kept in step with the player's gang.
/// @coney-only debug tooling: the original has no spawner
class FighterSides {
  public:
    /// Fighters' gangs made in `gangs` (which must outlive this).
    explicit FighterSides(Gangs& gangs) : m_gangs(&gangs) {}

    /// Puts `fighter` (a brain of the same brains as `gangs`) in a gang of `side` (Gangs::addMember()), and returns
    /// that gang's id (-1 when none could be made: every one of the 32 slots is in use, and the fighter joins none,
    /// so it is nobody's friend). `player` is the brain whose gang the sides are relative to; his gang may be null
    /// (his own scripts have not made one). Every live gang of the side is brought in line with the player's gang
    /// first, the full ones too.
    ///
    /// - **Enemy**: a gang of kind kFighterGangKind, enemies of the player's gang and of every ally gang, both ways.
    /// - **Ally**: a gang of the player's gang's kind (the Warriors' kind with no gang), so its members are his friends
    ///   by the kind rule and made friends of his gang too; it takes over his gang's enemies and friends (so it fights
    ///   whom he fights, the enemy fighters among them), brought up to date at every join. It is a gang of its own,
    ///   not his gang: a gang holds at most kGangMembers brains, and Coney's Gangs::addMember() drops the first of a
    ///   full gang (a Coney choice; the original adds the member anyway), so fighters added to the player's own gang
    ///   would push his crew out of it, and his crew's follow tactic would steer them.
    /// - **Neutral**: a gang of kind kNeutralGangKind with no enemy or friend bit: nobody's friend or enemy, until its
    ///   brain is hit and the original's own hit answer makes the two gangs enemies.
    ///
    /// A gang that is full (its kind's capacity) is left for a new one of the same side, named after the first with a
    /// number.
    int join(FighterSide side, Brain& fighter, const Brain& player);

    /// The side whose gang `gang` is, when one of this made it and it is still the gang it made.
    [[nodiscard]] std::optional<FighterSide> sideOf(const Gang* gang) const;

  private:
    // A gang this made: its slot and the name it was made under, which says it is still ours (a script may delete the
    // gang and reuse the slot).
    struct Made {
        int id = -1;
        std::string name;
    };

    // The first gang of `side` with room for one more, made when there is none; nullptr when none can be made.
    Gang* gangWithRoom(FighterSide side, const Gang* playersGang);
    // `made` as the gang it was made as, in use and unchanged; nullptr otherwise.
    [[nodiscard]] Gang* alive(const Made& made) const;
    // The gangs of `side` still alive.
    [[nodiscard]] std::vector<Gang*> gangsOf(FighterSide side) const;
    // Brings `gang`'s relations in line with the player's gang and the other sides' gangs.
    void relate(FighterSide side, Gang& gang, Gang* playersGang);

    Gangs* m_gangs;
    std::array<std::vector<Made>, 3> m_made;
};

} // namespace coney::ai
