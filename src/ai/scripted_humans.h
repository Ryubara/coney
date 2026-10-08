// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "ai/spawners.h"
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
    void setRageMode(double human, bool on) override;
    /// The brain's reachable mark (BrainSenses::reachable).
    void markReachable(double human, bool reachable) override;
    /// What the tagging query set with setTaggingQuery() answers; false without one.
    [[nodiscard]] bool tagging(double human) const override;
    /// Whether a human is spraying a tag now: what the play mode's tagging knows (player 1's stick game, the spots'
    /// AI taggers).
    using TaggingQuery = std::function<bool(double human)>;
    /// Sets the tagging query; an empty one answers false.
    void setTaggingQuery(TaggingQuery query) { m_tagging = std::move(query); }
    /// `HuSetArrested` (docs/research/crimes.md#arrest). Arrest (`Human_Arrest`): the human is cuffed
    /// (Human::setArrested()), its brain reset (`Brain_OnArrested`: the actions cleared, the target dropped, the goals
    /// popped down to a FindEnemy goal or all of them), event 17 with value 1 delivered, then the arrest hook. Release
    /// (`Human_Unarrest`): the hook first, then the human freed and event 17 with value 0. Nothing for a human already
    /// in that state. **Coney's readings**: event 17's other human is none (Coney keeps no grab partner on the
    /// record); the leaderless gang's follow after a release is not built.
    /// @orig 0x0022ec18 Human_Arrest (unknown)
    /// @orig 0x0022ef58 Human_Unarrest (unknown)
    /// @orig 0x0028c5d8 Brain_OnArrested (unknown)
    void setArrested(double human, bool arrested) override;
    /// What the game does with an arrest or a release beyond the human and its brain (the kind-0 record, the
    /// `arrested` line): called with the brain and whether it is now arrested.
    using ArrestHook = std::function<void(Brain& brain, bool arrested)>;
    /// Sets the arrest hook; an empty one does nothing.
    void setArrestHook(ArrestHook hook) { m_arrestHook = std::move(hook); }
    void setPushable(double human, bool pushable) override;
    void setMoney(double human, int dollars) override;
    /// Kept with a 100 % chance (`+0x278` = 100).
    void setCarriedItem(double human, std::string_view object) override;
    void setMugCallback(double human, std::string_view callback) override;
    /// The human's knocked-out mark (human::ScriptState::knockedOut) and its brain off, or both back.
    void setConscious(double human, bool conscious) override;
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
    void placeHatOnHead(double human, std::string_view hat, const std::function<double()>& nextHandle) override;
    /// Keeps the clip in the slot when `loaded`; slot 0 makes the human unpushable while it is set. While calls are
    /// held it answers `loaded` and sets the slot when the human is made.
    bool useAnim(double human, int slot, std::string_view anim, bool loaded) override;
    bool useAnyAnim(double human, std::uint32_t animId, std::string_view anim, bool loaded) override;
    /// Nothing when player 1 is in that gang already; otherwise the gang is noted (playerGang()) and player 1 handed
    /// to its next player (ScriptedBrains::changePlayerGang()). Coney has one player, so there is no second to hand
    /// over. Research: docs/research/characters.md#players
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
    /// Claims the flag for the human and pushes a TagGoal, which frees it at its end; the spray starts through the
    /// tag start set with setTagStart().
    void goalTag(const script::TagCall& call) override;
    /// What the tag goal starts a spray with (`Human_Tag`) and how it makes a tag object blank.
    using TagStart = std::function<void(double human, double tag, double flag)>;
    using TagBlank = std::function<void(double tag)>;
    void setTagStart(TagStart start, TagBlank blank) {
        m_tagStart = std::move(start);
        m_tagBlank = std::move(blank);
    }
    /// Adds the spawner to its gang's (at most 4), which spawners() runs.
    void addSpawner(const script::SpawnerCall& call) override;
    /// The gangs made to respond (named `Responder<n>`) that are not the police's are deleted; the police's are left.
    /// Coney's crimes send none yet, so a level has none unless its scripts made them.
    void clearResponders() override;
    /// **Coney stand-in**: Coney keeps no wanted state yet, so nothing is wanted to clear.
    void clearWanted(int gang) override;
    void setInvincible(int gang, bool on) override;
    void setTargetable(int gang, bool on) override;
    void setGangDamageResponse(int gang, int response) override;
    void setGangIcon(int gang, std::string_view object, int param) override;
    /// Takes the rage handlers, and the formations' default slots for the formations made later (Formations::
    /// keepDefaults()): a configuration call never rewrites the slots of a formation in use.
    void applyRules(const CharacterRules& rules) override;
    /// `CfgSetDefaultFollowSlotSet`: set `set`'s slots written into every formation (Formations::setDefaults()).
    void setDefaultFollowSlots(int set, std::span<const std::pair<float, float>> slots) override;

    /// Calls the rage handlers for what changed since the last call on each bound human's rage meter: `onFull` when
    /// it filled, with `(human, true)` (`Human_AddRage`); `onEnter` when rage started and `onExit` when it ended, with
    /// `(human, flag)`, the flag true when no other human rages (the original's count of ragers below 1; counted
    /// before the starter and after the ender, **Coney's reading**). docs/research/combat.md#rage. **Coney choice** of
    /// when: after the characters' step, as the animation callbacks run.
    void runRageHandlers();

    /// The gangs' spawners (`GangAddSpawner`, `GangStartSpawner`); the play mode updates them.
    [[nodiscard]] Spawners& spawners() { return m_spawners; }
    [[nodiscard]] const Spawners& spawners() const { return m_spawners; }
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
    Spawners m_spawners;
    std::map<double, double> m_reservations; // flag -> the human using it
    int m_playerGang = -1;
    std::uint64_t m_gangChangeMs = 0;
    TaggingQuery m_tagging;
    ArrestHook m_arrestHook;
    TagStart m_tagStart;
    TagBlank m_tagBlank;
};

} // namespace coney::ai
