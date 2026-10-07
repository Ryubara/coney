// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The game-state fields the story missions' scripts set beyond the first mission's (warriors/character_rules.h): the
// Warrior commands' retaliation switch and callback, the command menu's lock and display, the detail bytes, the outdoor
// flag, the music switches, the spawn cap, the tagging set-up, the civilians' and the AI's configuration and the
// models a level asks to keep. Each field cites where the original keeps it. GameState holds one
// (warriors/game_state.h); a level reset (resetForLevel()) clears what the original's level reset clears.
// Research: docs/references/bindings/story.md, docs/research/ai.md#warrior-commands

namespace coney {

/// The players a Warrior command field has a slot for (game state `+0x418`, `+0x42e`: one byte per player).
inline constexpr std::size_t kStoryPlayers = 2;
/// The detail bytes `setDetailFlag` writes (`W_GameState + 0x3e8`).
inline constexpr std::size_t kDetailBytes = 4;
/// The points `HuTagPattern` keeps (64 (x, y) pairs at `0x006cd978`).
inline constexpr std::size_t kTagPatternPoints = 64;
/// The system music's moods (calm, fight, hunted) and the tracks each holds (`0x0010e9e0`).
inline constexpr std::size_t kMusicMoods = 3;
inline constexpr std::size_t kMoodTracks = 3;
/// The models `SetCharacterModel` keeps (resource manager `+0xb4`, 32 slots).
inline constexpr std::size_t kKeptModels = 32;

/// What the story missions' scripts set in the game state.
struct StoryState {
    /// `WCLockCommands` (`+0x418` + player): set, a Warrior hit by his chief does not turn on him.
    std::array<bool, kStoryPlayers> noRetaliation{};
    /// The command menu locked (`+0x42e` + player): set by a Warrior's retaliation (not built), read by the dispatcher.
    std::array<bool, kStoryPlayers> menuLocked{};
    /// `WCSetCallback` (`+0x2fc`): the Lua function the dispatcher calls after each command it starts; empty for none.
    std::string commandCallback;
    /// `HUDShowWarCommand` (HUD element `+0x155c`): the command display may open. **Coney stand-in**: Coney's HUD has
    /// no command display yet, so the flag is only kept.
    std::array<bool, kStoryPlayers> commandDisplay{true, true};
    /// `setDetailFlag` / `clearDetailFlag` (`+0x3e8`): nothing reads them in the original.
    std::array<std::uint8_t, kDetailBytes> detailFlags{};
    /// `CfgSetOutdoorMode` (`+0x3e4`); what it changes is not traced.
    bool outdoor = false;
    /// `CfgDisableMusicForScenes` (`0x005148ac`); its reader is not traced.
    bool noMusicInScenes = false;
    /// `CfgGangSizeForCombatMusic` (`0x005148a8`): the rival gang size that can start the fight music. **Coney
    /// choice** before any call (not traced): 1.
    int combatMusicGangSize = 1;
    /// `SetSpawnMax` (`+0x434`): the spawned characters' cap. **Coney choice** before any call: no cap read yet, 0.
    int spawnMax = 0;
    /// `CfgEnableGrappleCounters` (`+0x56e3`). **Coney choice** before any call (not traced): on.
    bool grappleCounters = true;
    /// `CfgCivilianAggression` (`0x00510ad8`, `0x00510ad9`): 20 % each by default.
    std::array<int, 2> civilianAggression{20, 20};
    /// `CfgVerticalSightModifier` (`0x00510ad4`): 1.2 by default.
    float verticalSight = 1.2F;
    /// `CfgTagStartCallback` (`0x006b6870`): empty for none.
    std::string tagStartCallback;
    /// `HuTagPattern`: the point count (`0x00510914`) and the points (`0x006cd978`), (x, y) pairs.
    std::uint32_t tagPatternCount = 0;
    std::array<float, kTagPatternPoints * 2> tagPattern{};
    /// `SetCharacterModel`: the character types whose models the level keeps loaded, at most kKeptModels.
    /// **Coney stand-in**: Coney loads a model when a human needs it, so the list is only kept.
    std::vector<int> keptModels;
    /// `SoundSetEffect` and `SoundEnableEffects`: the reverb's type, depth, delay and feedback, and whether it is on.
    /// **Coney stand-in**: the mixer has no reverb yet, so they are only kept.
    int reverbType = 0;
    float reverbDepth = 0.0F;
    int reverbDelay = 0;
    int reverbFeedback = 0;
    bool reverbOn = false;
    /// The system music (audio manager `+0x64`): on (`SoundEnableSystemMusic`, game state `+0x3f8`), each mood's track
    /// hashes (`SoundSetMusicTrack`, up to kMoodTracks: 0 calm, 1 fight, 2 hunted) and the mood playing (-1: a new pick
    /// is due).
    bool systemMusic = false;
    std::array<std::vector<std::uint32_t>, kMusicMoods> moodTracks;
    int musicMood = -1;
    /// `StartGarbage` / `EndGarbage`: the litter kind blowing round the camera, -1 for none. **Coney stand-in**: the
    /// litter is not drawn yet.
    int garbage = -1;
    /// `CfgChanceToGetHelp` (`0x00510adf`): the chance, 0-100, that a civilian who sees an attack calls for help.
    /// **Coney stand-in**: civilians do not call for help yet, so it is only kept; **Coney choice** until set (the
    /// default is not on the page): 0.
    int chanceToGetHelp = 0;
    /// `CfgWarriorWeapons` (game state `+0x5704`, 1 from the constructor and the level reset): the AI Warriors may pick
    /// up weapons. **Coney stand-in**: Coney's AI Warriors search for no pickups yet, so it is only kept.
    bool warriorWeapons = true;

    /// The level reset's part (`0x00418c68`): the retaliation switches, the menu locks, the callback and the detail
    /// bytes cleared. The Warrior commands' enables are CharacterRules' (enabled again by the caller).
    void resetForLevel() {
        noRetaliation.fill(false);
        menuLocked.fill(false);
        commandCallback.clear();
        detailFlags.fill(0);
        garbage = -1;
        warriorWeapons = true;
    }
};

} // namespace coney
