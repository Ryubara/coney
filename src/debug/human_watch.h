// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ai/brain.h"
#include "ai/goal.h"
#include "debug/entity_inspect.h"
#include "human/human.h"
#include "human/human_animator.h"
#include "human/locomotion.h"
#include "human/target_human.h"

// The Entity inspector's view of a human (docs/guides/debug-menu.md#entity-inspector): every value Coney keeps on a
// human::Human and its brain, as watch lines in groups. Read through the human's and the brain's public accessors
// only, so src/human and src/ai hold no debug code.

namespace coney::debug {

/// What a play mode knows of a human beyond the human itself: who it is to the scene.
struct HumanFacts {
    EntityId id = kNoEntity;
    std::string role;                 ///< "player 1", "AI fighter", "cast", "target".
    std::string model;                ///< The Character List model it is drawn as; empty when not known.
    std::optional<int> type;          ///< Its `CfgChar` character type, when known.
    const ai::Brain* brain = nullptr; ///< Its brain; null for none (a sandbox target, a player without a cast).
    bool frozen = false;              ///< The Player page's freeze holds it.
};

/// Everything known about `human`, as the inspector's groups: Identity, Transform, Locomotion, Animation, State flags,
/// Meters, AI (with a brain), Physics and Combat.
[[nodiscard]] std::vector<WatchGroup> describeHuman(const human::Human& human, const HumanFacts& facts);

/// Everything known about the sandbox target `target`: Identity, Transform, Animation, Meters and Combat. A target is
/// Coney's passive stand-in human (human::TargetHuman), with no body in the physics and no brain.
[[nodiscard]] std::vector<WatchGroup> describeTarget(const human::TargetHuman& target, const HumanFacts& facts);

/// A gait's short name ("standing", "walk", "sprint").
[[nodiscard]] std::string_view gaitName(human::Gait gait);
/// An animation state's short name ("idle", "move", "attack", ...).
[[nodiscard]] std::string_view animStateName(human::AnimState state);
/// A brain type's short name ("player", "gang", "civilian", ...).
[[nodiscard]] std::string_view brainTypeName(ai::BrainType type);
/// A goal type's name as the code names it ("Fight", "MoveToFlag", ...).
[[nodiscard]] std::string_view goalTypeName(ai::GoalType type);
/// The names of the human flags (human/human_flags.h) set in `flags`, separated by spaces; "-" for none, and any bit
/// the code does not name as hex.
[[nodiscard]] std::string humanFlagNames(std::uint64_t flags);

} // namespace coney::debug
