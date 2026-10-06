// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "characters/anim_set.h"
#include "combat/commands.h"
#include "combat/stick.h"

// The 45 attack kinds an AI fighter chooses from: the command each one presses (the same ids a pad makes), the chains
// a kind is pressed as, the delay between a chain's presses, the class attack tables (`Att_*`) that weight them, and
// the weighted pick. Pure functions over a seeded generator, so a pick is the same on every run.
// Research: docs/research/ai.md#attack-kinds, docs/research/ai.md#fight

namespace coney::ai {

/// How many attack kinds there are (the `Att_*` tables' and `CfgAttackDelay`'s length).
inline constexpr std::size_t kAttackKinds = 45;

/// A class's attack weights, one per kind (brain `+0x298`, the class's `Att_*` table): 0 never, larger more often.
using AttackWeights = std::array<std::uint8_t, kAttackKinds>;

/// The command attack kind `kind` presses (`AttackKind_ToCommand`, docs/research/ai.md#attack-kinds); kinds 24 and 35
/// press square or cross at random (a draw of `random`). Kind 18 and kinds outside 0-44 press nothing (0).
/// @orig 0x00231090 AttackKind_ToCommand (unknown)
[[nodiscard]] combat::CommandId commandOf(int kind, combat::CombatRandom& random);

/// The kinds `kind` is pressed as, in order (`Brain_QueueAttack`): kind 2 is 0 then 2 (`X1`, `XX2`), kind 6 is 1, 5
/// and 6 (`S1`, `SS2`, `SSX3`) and kind 8 is 1, 5 and 8, as the research reads; **Coney's reading** of the others it
/// does not list, by the same rule (a chain is pressed from its first attack): 3 is 1 then 3 (`SX2`), 4 is 0 then 4
/// (`XS2`), 5 is 1 then 5 (`SS2`), 7 is 1, 5 and 7 (`SSS3`), 9 is 1, 5 and 9. Every other kind is pressed alone.
/// @orig 0x0028e248 Brain_QueueAttack (unknown)
[[nodiscard]] std::vector<int> chainOf(int kind);

/// The anim id whose clip times the press after a press of `kind` (`AttackKind_ChainDelay`): 11 `X1` after kind 0,
/// 12 `S1` after kind 1, 16 `SS2` after kind 5; nothing after any other kind.
[[nodiscard]] std::optional<std::uint32_t> chainDelayClip(int kind);

/// The delay before the press after a press of `kind`, in milliseconds: the time of the first event of
/// chainDelayClip()'s clip in `anims` (its frame / 30 s), so the next press lands in the chain's window as a player's
/// would; 0 when the kind has no such clip or the clip is missing or has no events.
/// @orig 0x00231198 AttackKind_ChainDelay (unknown)
[[nodiscard]] int chainDelayMs(int kind, const characters::AnimSet& anims);

/// The attack an attack kind starts with, whose far range decides whether the target is in reach: `X1` (11) for a
/// kind that presses cross first, `S1` (12) for every other.
[[nodiscard]] std::uint32_t firstAnimOf(int kind);

/// Whether Coney's humans can play attack kind `kind` against another human: its chain presses only square or cross
/// (`0xf`, `0x10`) and the chain's steps (`0x11`, `0x12`), so the strikes and their chains; kind 10, the snap, too.
/// **Coney choice**, standing in for the pick's filter "what the human can do now" (`0x002240e8`, not traced): the
/// grabs and tackles between two humans, the specials, the moving attacks, the `SSS3` holds (kinds 8 and 9) and the
/// commands no Coney path takes are left out.
[[nodiscard]] bool playable(int kind);

/// The weighted pick (`Brain_PickAttack`): a kind drawn from `weights` among the playable() kinds, each with the chance
/// of its weight over their sum; nothing when they sum to 0. One draw of `random`. **Coney choice**: the adjustments
/// for the number of attackers and the grab chance are not traced, so the weights are taken as they are.
/// @orig 0x0028e708 Brain_PickAttack (unknown)
/// @orig 0x002911f8 Brain_GetAttackWeight (unknown)
[[nodiscard]] std::optional<int> pickAttack(const AttackWeights& weights, combat::CombatRandom& random);

/// `Att_Normal`, the sparring and fence Warriors' and the street civilian's table (docs/research/ai.md#level99), for
/// when the disc's configuration is not read.
[[nodiscard]] AttackWeights attNormal();
/// `Att_Bum`: square chains only.
[[nodiscard]] AttackWeights attBum();

/// A whole number in [`low`, `high`] from `random` (the brain's rolls: a random 750-1000 ms, rand100).
[[nodiscard]] int rollRange(combat::CombatRandom& random, int low, int high);

} // namespace coney::ai
