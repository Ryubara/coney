// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string_view>
#include <vector>

#include "scripting/human_bindings.h"
#include "warriors/character_rules.h"

// The level scripts' hold on the humans, their brains and their gangs for the character bindings
// (scripting/human_bindings.h): the humans the scripts name by handle (ai::ScriptedBrains) get their flags, health,
// rage, arrest, pad lock and commands, and what they carry; the brains their back-off, bum and use-flag goals; the
// gangs their invincibility and targeting. Like the AI bindings, a call on a human not made yet waits until the level
// makes it (ScriptedBrains::hold()). After each characters' step it calls the rage handlers the configuration named.
// Research: docs/references/bindings/character.md, docs/references/bindings/ai.md, docs/references/bindings/gang.md

namespace coney::ai {

class Brain;
class ScriptedBrains;

/// The character bindings' host on the brains a level's scripts drive.
class ScriptedHumans final : public script::HumanBindingHost {
  public:
    /// The hold on `scripted`'s brains (which must outlive it).
    explicit ScriptedHumans(ScriptedBrains& scripted) : m_scripted(&scripted) {}

    // ---- script::HumanBindingHost: each does nothing for a handle with no human or an id with no gang ----

    /// Its human's flags, health, arrest; a player's brain is player 1's; its gang's kind; the object in its hands.
    [[nodiscard]] std::optional<script::HumanStatus> status(double handle) const override;
    void setFlags(double human, std::uint64_t bits, bool on) override;
    void setLockedRage(double human, bool on) override;
    void fillRage(double human, int holdMs) override;
    void setRageFraction(double human, float fraction) override;
    void setHealthPercent(double human, float percent) override;
    void revive(double human) override;
    void setNormalMode(double human, bool full) override;
    void setArrested(double human, bool arrested) override;
    void setPushable(double human, bool pushable) override;
    void setMoney(double human, int dollars) override;
    /// Kept with a 100 % chance (`+0x278` = 100).
    void setCarriedItem(double human, std::string_view object) override;
    void setMugCallback(double human, std::string_view callback) override;
    void setSoundCommands(double human, bool on) override;
    void setPocket(double human, int item, int count) override;
    /// Kept while both humans (or the target object) exist.
    void setLookTarget(const script::LookTargetCall& call) override;
    /// Puts an AI human beside `near`: on its right, the two bodies' radii apart, at its height, keeping its heading.
    /// **Coney choice**: the original's search for a free spot is not traced; player 1 is not moved (his teleports
    /// are the level's, gamemodes/gameplay_mode.h).
    void teleportNear(double human, double near) override;
    void lockPad(double human, bool locked) override;
    /// Only for a human a pad drives (player 1's brain, not dead): a command's bit, or every one with `command` 0.
    void enableCommand(double human, int command, bool enabled) override;
    void setIcon(double human, std::string_view object, int param) override;
    /// **Coney stand-in**: Coney's objects have no weapon class yet, so whatever it holds is let go.
    void dropWeapon(double human) override;
    void releaseObject(double object) override;
    /// While calls are held the handle is given at once and the object put in the hand when the human is made.
    double placeItemInHand(double human, std::string_view object, const std::function<double()>& nextHandle) override;
    /// Keeps the clip in the slot when `loaded`; slot 0 makes the human unpushable while it is set. While calls are
    /// held it answers `loaded` and sets the slot when the human is made.
    bool useAnim(double human, int slot, std::string_view anim, bool loaded) override;
    /// **Coney stand-in**: Coney has one player and no second pad, so the take-over of the gang's members is not
    /// built; nothing happens when player 1 is in that gang already, otherwise the gang is noted (playerGang()).
    void changePlayerGang(int gang, bool stamp) override;
    /// 0 for player 1's human.
    [[nodiscard]] std::optional<int> playerIndex(double handle) const override;
    /// Pops the top goal when it is a back-off.
    /// @orig 0x00292cf0 Brain_ClearBackoff (unknown)
    void clearBackoff(double human) override;
    void setWantsWeapon(double human, bool wants) override;
    void setDamageResponse(double human, int response) override;
    void goalBackoff(const script::BackoffCall& call) override;
    void goalBumLogic(const script::BumLogicCall& call) override;
    /// Reserves the flag for the human and pushes the goal, which frees it at its end.
    void goalMoveToUseFlag(const script::MoveToUseFlagCall& call) override;
    /// Keeps the spawner on its gang (at most 4). **Coney stand-in**: spawners do not spawn yet.
    void addSpawner(const script::SpawnerCall& call) override;
    /// The gangs made to respond (named `Responder<n>`) that are not the police's are deleted; the police's are left.
    /// Coney's crimes send none yet, so a level has none unless its scripts made them.
    void clearResponders() override;
    /// **Coney stand-in**: Coney keeps no wanted state yet, so nothing is wanted to clear.
    void clearWanted(int gang) override;
    void setInvincible(int gang, bool on) override;
    void setTargetable(int gang, bool on) override;
    void setGangDamageResponse(int gang, int response) override;
    /// Takes the rage handlers and the formations' default slots.
    void applyRules(const CharacterRules& rules) override;

    /// Calls the rage handlers for what changed since the last call on each bound human's rage meter: `onFull` when
    /// it filled, `onEnter` when rage started, `onExit` when it ended, each with the human's handle (**Coney choice**
    /// of the arguments and of when: after the characters' step, as the animation callbacks run).
    void runRageHandlers();

    /// The spawners kept on gang `gang`.
    [[nodiscard]] const std::vector<script::SpawnerCall>& spawners(int gang) const;
    /// Who holds the flag with `handle` (0 for no one).
    [[nodiscard]] double reservation(double flag) const;
    /// reservation(), for `FlagGetOwner`.
    [[nodiscard]] double flagUser(double flag) const override { return reservation(flag); }
    /// The gang `HuChangePlayerGang` last named (-1 for none), and the game time of the last stamped change.
    [[nodiscard]] int playerGang() const { return m_playerGang; }
    [[nodiscard]] std::uint64_t gangChangeMs() const { return m_gangChangeMs; }

  private:
    // Runs `body` on the brain named by `handle` now, or when the level makes it while calls are held; nothing when
    // no brain has the handle.
    void onBrain(double handle, const std::function<void(Brain&)>& body);
    // The rage meter as runRageHandlers() last saw it on one human.
    struct RageSeen {
        bool full = false;
        bool raging = false;
    };

    ScriptedBrains* m_scripted;
    RageHandlers m_rage;
    std::map<double, RageSeen> m_rageSeen;
    std::map<int, std::vector<script::SpawnerCall>> m_spawners;
    std::map<double, double> m_reservations; // flag -> the human using it
    int m_playerGang = -1;
    std::uint64_t m_gangChangeMs = 0;
};

} // namespace coney::ai
