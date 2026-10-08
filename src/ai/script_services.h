// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

// What the scripted goals, the gangs and the tactics ask of the world around the brains: the brains by their humans'
// script handles, the player, the script system (a goal's callback, a gang's message handler, a tactic's callback),
// the scene system (`GoalAddressPerson`'s speech) and the clips a brain plays by id (the dynamic slot 668, a crowd's
// cheers). Every hook but brain() does nothing by default, so a world without scripts, scenes or clips still runs the
// goals: a clip that cannot start ends its action at once, as the original's play-anim action does when its clip
// fails.
// Research: docs/research/ai.md#scripted, docs/research/ai.md#gang-events, docs/research/ai.md#tactics

namespace coney::ai {

class Brain;
struct BrainEvent;

/// A goal's Lua callback is scheduled this long after its End, ms: `ScheduleFuncArg2(name, handle, completed, 33)`
/// (docs/research/ai.md#scripted).
inline constexpr std::uint32_t kGoalCallbackDelayMs = 33;
/// The anim id of a human's dynamic animation slot (`GoalPlayDynAnimation`, docs/research/ai.md#dyn-animation).
inline constexpr int kDynamicAnimId = 668;

/// The services a brain's scripted goals, its gang and the gang's tactic use; the level's scripted brains
/// (ai::ScriptedBrains) give them. It must outlive every goal, gang and tactic that holds it.
class ScriptServices {
  public:
    ScriptServices() = default;
    ScriptServices(const ScriptServices&) = delete;
    ScriptServices& operator=(const ScriptServices&) = delete;
    ScriptServices(ScriptServices&&) = delete;
    ScriptServices& operator=(ScriptServices&&) = delete;
    virtual ~ScriptServices() = default;

    /// The brain of the human whose script handle is `handle`; null when none.
    [[nodiscard]] virtual Brain* brain(double handle) const = 0;
    /// Player 1's brain; null when there is none.
    [[nodiscard]] virtual Brain* player() const { return nullptr; }
    /// Puts `human` on both radars as a blip of `type` with `icon` at `factor` (a dealer's greeting,
    /// `DealerGoal_AddRadarIcon`), unless it has one already; nothing without a HUD.
    virtual void addRadarIcon(Brain& /*human*/, int /*type*/, int /*icon*/, float /*factor*/) {}

    /// Schedules the Lua function `function` with `args` (at most two) `delayMs` of game time from now
    /// (`ScriptSystem::Schedule*`).
    virtual void schedule(std::string_view /*function*/, std::span<const double> /*args*/, std::uint32_t /*delayMs*/) {}
    /// Calls the Lua function `function` with `args` now. Returns whether it returned a true value (anything but
    /// nil); false when there is no script state or no such function.
    virtual bool call(std::string_view /*function*/, std::span<const double> /*args*/) { return false; }
    /// Offers `event` to `human`'s own script handlers (`SetMsgHandler`, `0x00384c38`). Returns whether a handler took
    /// it (only a message that asks for a result can be taken).
    virtual bool humanEvent(Brain& /*human*/, const BrainEvent& /*event*/) { return false; }

    /// Starts scene `scene` with `human` in it; its end calls `callback` (empty for none) back (`0x003541a0`,
    /// `0x00353f40`).
    virtual void playScene(int /*scene*/, Brain& /*human*/, std::string_view /*callback*/) {}
    /// Whether scene `scene` has finished (`0x00354058`). True by default: with no scene system a scene is over at
    /// once.
    [[nodiscard]] virtual bool sceneFinished(int /*scene*/) const { return true; }
    /// Stops scene `scene` (`Scene_Stop`).
    virtual void stopScene(int /*scene*/) {}

    /// Asks for the level-loaded clip `name` in `human`'s dynamic slot (human `+0x468`, anim kDynamicAnimId).
    virtual void loadDynamicClip(Brain& /*human*/, std::string_view /*name*/) {}
    /// Whether the clip asked for is loaded (human `+0x488`). True by default.
    [[nodiscard]] virtual bool dynamicClipLoaded(const Brain& /*human*/) const { return true; }
    /// Frees `human`'s dynamic slot (`0x0010bcf8`).
    virtual void freeDynamicClip(Brain& /*human*/) {}
    /// Has `human` say speech command `command` (0-206, `Human_PlaySpeech`) to `target` (a handle he looks at, 0 for
    /// none); with `interrupt` it cuts off a line he is saying, else nothing plays while one does. Nothing without a
    /// sound system.
    virtual void say(Brain& /*human*/, int /*command*/, bool /*interrupt*/, double /*target*/) {}
    /// Starts anim `animId` on `human` (`0x0025a3e0`) and returns the record flags (`+0x08`) it holds while it plays;
    /// nothing when it cannot start. By default nothing starts: Coney's humans play no clip by id from outside their
    /// dispatcher yet.
    [[nodiscard]] virtual std::optional<std::uint32_t> playClip(Brain& /*human*/, int /*animId*/) {
        return std::nullopt;
    }
    /// Starts the level-loaded clip `name` on `human` as anim `animId` (a gang clip table's entry or an override
    /// `Human_SetAnimOverride` binds to the id), faded in over `fade` seconds; returns the record flags it holds, or
    /// nothing when the clip is not loaded. By default nothing starts.
    [[nodiscard]] virtual std::optional<std::uint32_t> playNamedClip(Brain& /*human*/, int /*animId*/,
                                                                     std::string_view /*name*/, float /*fade*/) {
        return std::nullopt;
    }
    /// Whether the level-loaded clip `name` is available (the level's animation cache, `0x0016f980`). False by
    /// default.
    [[nodiscard]] virtual bool clipAvailable(std::string_view /*name*/) const { return false; }
};

} // namespace coney::ai
