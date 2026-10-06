// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// The characters' rules the configuration and level scripts set in the game state and its globals, which every
// script state shares and the level's humans, brains and gangs read: the spotting and mugging switches, the lifetime
// of temporary things, the rage handlers, the formations' default slots, the interrogation overrides, the Warriors'
// commands and the level's dynamic animations. GameState holds one (warriors/game_state.h).
// Research: docs/references/bindings/config.md, docs/references/bindings/character.md#setdynamicanimation,
// docs/references/bindings/ai.md#setinterrogateparam

namespace coney {

/// The rage handlers (`CfgRageHandlers`): three Lua function names and two times.
struct RageHandlers {
    std::string onEnter;              ///< `0x006b6830`: called when rage starts; empty for none.
    std::string onExit;               ///< `0x006b6850`: called when rage ends.
    std::string onFull;               ///< `0x006b6810`: called when the meter fills.
    std::uint32_t durationMs = 20000; ///< `0x00510294` (its use is inferred: rage's length).
    std::uint32_t holdMs = 5000;      ///< `0x00510290`: a rage gain's hold before the meter decays.
};

/// One interrogation override set (`SetInterrogateParam`, `0x00510a18` / `0x00510a38`): in force while timeA is not
/// 0. What each value controls is an open question (docs/references/bindings/ai.md#setinterrogateparam).
struct InterrogateOverride {
    std::array<std::uint8_t, 3> values{};   ///< The three byte values.
    std::array<std::uint32_t, 4> timesMs{}; ///< timeA, timeB, timeC, timeD.
    std::array<float, 2> anglesRadians{};   ///< angleA, angleB, given in degrees.
    std::uint8_t flag = 0;                  ///< The byte flag.
    [[nodiscard]] bool active() const { return timesMs[0] != 0; }
};

/// How many follow slot sets `CfgSetDefaultFollowSlotSet` writes, and the positions of each it keeps.
inline constexpr std::size_t kDefaultFollowSets = 2;
inline constexpr std::size_t kDefaultFollowSlots = 9;
/// The level's dynamic animation list (`SetDynamicAnimation`).
inline constexpr std::size_t kDynamicAnimations = 64;
/// The Warrior commands of a war chief (game state `+0x41e`, 7 bytes per player), and the players.
inline constexpr std::size_t kWarriorCommands = 7;
inline constexpr std::size_t kWarriorPlayers = 2;

/// The characters' rules of the game state.
struct CharacterRules {
    bool playerMugging = true;         ///< `+0x5708` (`CfgPlayerMugging`): a player may mug the other player's human.
    bool enemySpotting = true;         ///< `+0x56f8` (`CfgSetEnemySpotting`): enemies spot the player.
    bool warriorSpotting = true;       ///< `+0x56fc` (`CfgSetWarriorSpotting`, which can only clear it).
    std::int32_t timeToLiveMs = 30000; ///< `+0x26c` (`CfgSetGlobalTimeToLive`): temporary things' lifetime.
    RageHandlers rage;
    /// The formations' default slots by set (`CfgSetDefaultFollowSlotSet`): (x, y) m, x to the leader's right and y
    /// ahead; nothing until a script sets the set.
    std::array<std::optional<std::array<std::pair<float, float>, kDefaultFollowSlots>>, kDefaultFollowSets> followSlots;
    std::array<InterrogateOverride, 2> interrogate; ///< The two override sets.
    /// The Warriors' commands enabled per player (`WCEnableAllCommands`), the last one each player issued (-1 none;
    /// `WCIssueCommand`) and the commands' lock (game state `+0x411`). **Coney choice**: all enabled at the start (the
    /// tutorial turns them off until it teaches them).
    std::array<std::array<bool, kWarriorCommands>, kWarriorPlayers> warriorCommands{
        {{true, true, true, true, true, true, true}, {true, true, true, true, true, true, true}}};
    std::array<int, kWarriorPlayers> lastWarriorCommand{-1, -1};
    bool warriorCommandsLocked = false;
    /// The animation files the level asked the resource manager for (`SetDynamicAnimation`), at most
    /// kDynamicAnimations.
    std::vector<std::string> dynamicAnimations;
};

} // namespace coney
