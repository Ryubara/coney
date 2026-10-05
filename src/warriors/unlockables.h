// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <set>
#include <utility>

namespace coney {

/// The kinds of unlockable the Rumble menu asks about, the second argument of the unlock check `0x00424130` on the
/// unlockables manager (`0x006fe998`), docs/research/frontend.md#rumble-data.
enum class UnlockKind : std::uint8_t {
    RumbleMode = 1,    ///< A Rumble game mode, by its `RM_*` id (`CfgRumbleGame`).
    RumbleArena = 2,   ///< A Rumble arena, by its level number (`CfgRumbleArena`).
    RumbleGang = 3,    ///< A Rumble gang, by its gang id (`CfgRumbleGang`).
    CharacterType = 4, ///< A character type a gang's roster names (the stand-in table `0x001ec490`).
};

/// What the player has unlocked, as the Rumble menu asks it: Coney's stand-in for the unlockables manager, whose
/// records (`UM_SetUnlockable`) and story unlocks (`UM_Unlock`) Coney does not have yet. It answers "unlocked" for the
/// (kind, id) pairs it holds and "locked" for every other, as the original's check answers for an id no record
/// unlocks (docs/references/bindings/level.md#um_isdataunlocked).
///
/// **Coney's choice** for a fresh profile (freshProfile()), from what a fresh boot shows
/// (docs/research/frontend.md#rumble-setup): the modes 1 ON 1 (12) and WAR PARTY (14), the Fight Pen (102), and the
/// gangs BASEBALL FURIES (5) and ORPHANS (3); no character type, so every type the stand-in table names is replaced.
/// Which ids the story unlocks is not researched, so everything else stays locked until it is.
///
/// Research: docs/research/frontend.md#rumble-data
class Unlockables {
  public:
    /// What a fresh profile has unlocked (above).
    [[nodiscard]] static Unlockables freshProfile();

    /// Whether `id` of `kind` is unlocked.
    [[nodiscard]] bool isUnlocked(UnlockKind kind, std::int64_t id) const { return m_unlocked.contains({kind, id}); }
    /// Unlocks `id` of `kind`.
    void unlock(UnlockKind kind, std::int64_t id) { m_unlocked.insert({kind, id}); }
    /// Locks everything.
    void clear() { m_unlocked.clear(); }

  private:
    std::set<std::pair<UnlockKind, std::int64_t>> m_unlocked;
};

} // namespace coney
