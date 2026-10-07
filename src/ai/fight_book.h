// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>

#include "ai/attack_places.h"
#include "ai/fight_checks.h"

namespace coney::script {
class RecordedCalls;
} // namespace coney::script

// What a brain keeps for its fights beyond its target and slots: the active-attacker places and spacing bytes it
// gives those who attack it, and its tackle meter; and the per-gang fight values `CfgGang` sets, which its attackers'
// gangs bring.
// Research: docs/research/ai.md#attack-places, docs/research/ai.md#try-tackle, docs/research/ai.md#fight-reactions

namespace coney::ai {

/// The fight values of one gang kind from `CfgGang(kind, v2, v3, v4, v5, strategy, v7, v8, v9)`: 0 until set.
struct GangFightValues {
    int standingSpacing = 0; ///< `v2` (`+0x6c`): how many of the gang may swing at one standing man at once.
    int downSpacing = 0;     ///< `v3` (`+0x6d`): the same at a downed man.
    int handOver = 0;        ///< `v5` (`+0x6f`): a grabber's hand-over chance, × 10 percent.
    int tackle = 0;          ///< `v7` (`+0x70`): the tackle try's threshold (0 never).
    int rearGrab = 0;        ///< `v8` (`+0x71`): the rear grab's chance, × 25 percent (0 never).
};

/// How many gang kinds `CfgGang` configures (ids 0-24).
inline constexpr std::size_t kGangKinds = 25;
/// The fight values by gang kind.
using GangFightTable = std::array<GangFightValues, kGangKinds>;

/// The table from the scripts' recorded `CfgGang` calls (the last for each kind); kinds with none keep 0s.
[[nodiscard]] GangFightTable gangFightTableFrom(const script::RecordedCalls& recorded);

/// A brain's fight books.
struct FightBook {
    ActivePlaces places; ///< `+0x1f0`: those about to swing at this brain's human.
    Spacing spacing;     ///< `+0x14a`, `+0x14b`.
    TackleMeter tackle;  ///< `+0x148`.
};

} // namespace coney::ai
