// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "animation/anim_math.h"
#include "scenes/scene_cache.h"
#include "scenes/scene_host.h"
#include "scenes/scene_list.h"

// The scene player: the scene task that plays a loaded scene (its start sequence, its humans, objects, camera and
// lights driven from their tracks at 30 updates a second, the segments streamed while it plays, the skip, the end
// that gives everything back) and the system the scene bindings work on. Pure: it reads no clock and no pads of its
// own, so a scene plays the same in a test as in the game.
// Research: docs/research/scenes.md

namespace coney::scenes {

/// The updates a scene task counts from its creation before a button skips it: 2 s at 30 a second.
inline constexpr std::uint32_t kSkipDelayUpdates = 60;
/// The letterbox's time in and out, seconds.
inline constexpr float kLetterboxSeconds = 1.5F;
/// The game time after which a scene that has not started is given up, ms.
inline constexpr std::uint64_t kStartTimeoutMs = 10'000;
/// The pad bits that skip a skippable scene: cross and START.
inline constexpr std::uint16_t kSkipCross = 0x0040;
inline constexpr std::uint16_t kSkipStart = 0x0800;

/// Which binding plays a scene, which decides its flags (docs/research/scenes.md#playing).
enum class PlayKind : std::uint8_t {
    Cinematic, ///< `ScenePlayCinematic`.
    Fixed,     ///< `ScenePlayFixedScene`.
    Placed,    ///< `ScenePlay`: at a given position and rotation.
    Animation, ///< `ScenePlayAnimation`: fitted to its humans.
};

/// What a play binding asks: the flags the task takes (`+0xed` cinematic to `+0xea` relative) and its arguments.
struct PlayRequest {
    PlayKind kind = PlayKind::Cinematic;
    std::uint32_t delay = 0; ///< Units of 60 updates the start waits for the humans to reach their marks.
    std::string onEnd{};     ///< The Lua function called with the id when it ends; empty for none.
    bool cinematic = false;  ///< `+0xed`: letterbox, preload, the player handed over.
    bool skippable = false;  ///< `+0xe5`.
    bool looping = false;    ///< `+0xeb`.
    bool freeze = false;     ///< `+0xef`: every brain suspended while it plays.
    bool final = false;      ///< `+0xe7`: no `<scene>_end.pak` preload at the end.
    bool chain = false;      ///< `+0xe8`: letterbox in at once; a START skip carries into it.
    float blendCam = -1.0F;  ///< `+0x94`: seconds of camera blend back; 0 or below is a cut.
    ScenePose place{};       ///< The scene's space: the origin and no rotation for the cinematic and fixed kinds.
};

/// What the scene system has done, for the log and the tests (counts only).
struct SceneStats {
    std::uint64_t preloads = 0;  ///< `ScenePreload` calls.
    std::uint64_t started = 0;   ///< Scenes that reached state 5.
    std::uint64_t ended = 0;     ///< Scenes that reached state 8.
    std::uint64_t skipped = 0;   ///< Of those, skipped.
    std::uint64_t aborted = 0;   ///< Given up after kStartTimeoutMs.
    std::uint64_t updates = 0;   ///< Scene task updates, all tasks.
    std::uint64_t events = 0;    ///< Track and clip events fired.
    std::uint64_t warps = 0;     ///< Role clip events 21 and 22 applied.
    std::uint64_t callbacks = 0; ///< Lua calls made (preload callbacks, the global callback, end functions).
};

class SceneTask;

/// The game's scenes: the scene list, the 12 slots, the playing scene tasks and the workers of the scene bindings
/// (`ScenePreload`, the play bindings, `SceneStop`, `GoalJoinCinematic`'s binding, `SceneAddObject`, ...). The host
/// (the play mode) is attached while there is one; without it the scenes play against SceneHost's defaults.
///
/// Coney's choices where the page is silent (docs/research/scenes.md#coneys-implementation): a task's first update
/// is the one after the play binding; a role's root motion moves its feet in all three axes and the host may settle
/// them on the ground; role clip events other than 21 and 22 are not acted on; a caption is passed to the host by
/// number, its text being unknown.
///
/// Research: docs/research/scenes.md
class SceneSystem {
  public:
    /// Calls a Lua function by name with numbers (the ScriptSystem's call); empty to call nothing.
    using ScriptCall = std::function<void(std::string_view function, std::span<const double> args)>;

    /// A system over `list` (which must outlive it) reading records through `source`, calling Lua through `call`.
    SceneSystem(const SceneList& list, SceneRecordSource source, ScriptCall call);
    ~SceneSystem();
    SceneSystem(const SceneSystem&) = delete;
    SceneSystem& operator=(const SceneSystem&) = delete;
    SceneSystem(SceneSystem&&) = delete;
    SceneSystem& operator=(SceneSystem&&) = delete;

    /// Attaches the game around the scenes (null: SceneHost's defaults). It must outlive the attachment.
    void setHost(SceneHost* host);
    /// Replaces the Lua caller (a new script state).
    void setScriptCall(ScriptCall call) { m_call = std::move(call); }

    // ---- The bindings' workers ----

    /// `ScenePreload(name, callback)`: the id of the first record whose name contains `name` (0 when none does),
    /// requested into a slot; `callback` is called with the id when it arrives.
    /// @orig 0x00353f88 Scene_Preload (SceneCache.cpp)
    std::uint32_t preload(std::string_view name, std::string_view callback);
    /// `SceneIsPreloaded(name)`: a slot holds that scene loaded and idle.
    [[nodiscard]] bool isPreloaded(std::string_view name) const;
    /// `SceneUnload(id)`: frees the slot while the scene is loaded and not playing.
    void unload(std::uint32_t id);
    /// `SceneSetCallback(name)`: the function every scene calls with no arguments as it starts; empty clears it.
    void setGlobalCallback(std::string_view name) { m_globalCallback = std::string(name); }
    /// The play bindings' worker: makes the scene task for `id` with `request`, loading the scene first when it is
    /// not loaded. Returns false when it cannot (no such scene, no slot, a failed read, the scene already playing).
    /// @orig 0x00353818 Scene_Play (SceneCache.cpp)
    /// @orig 0x003a13d0 SceneTask_Create (SceneTask.cpp)
    bool play(std::uint32_t id, const PlayRequest& request);
    /// `SceneStop(id, force)`: a starting scene ends at once; a playing one has its clips ended (state 6 while a role
    /// is busy, then 7); a looping one that is not forced only stops looping.
    /// @orig 0x00353a10 Scene_Stop (SceneCache.cpp)
    void stop(std::uint32_t id, bool force);
    /// `SceneDone(id)`: false while the scene is starting or playing, true otherwise (also when not loaded).
    [[nodiscard]] bool done(std::uint32_t id) const;
    /// `SceneLength(id)`: the first part's length in seconds (its first track's duration); 0 when not loaded or empty.
    [[nodiscard]] float length(std::uint32_t id) const;
    /// `GoalJoinCinematic(human, scene, role, gait)`: writes `human` into role `role` when the scene is loaded, the
    /// role exists and the human is in no playing scene; the host's humanJoin() takes away a player's control. Returns
    /// whether it was bound.
    /// @orig 0x003541a0 Scene_BindHuman (SceneCache.cpp)
    /// @orig 0x002e5300 Goal_JoinCinematic (unknown)
    bool joinHuman(double human, std::uint32_t id, std::size_t role, int gait);
    /// `SceneAddObject(id, object, slot)`: binds `object` to object slot `slot`; ignored past the scene's objects.
    /// @orig 0x00354280 Scene_BindObject (SceneCache.cpp)
    void addObject(std::uint32_t id, double object, std::size_t slot);
    /// `ScreenQueueEffect(type, seconds)` in play: the screen effect handed to the host, which owns player 1's view's
    /// effects (types 0 to 3 and 5; others are not passed on). **Coney's glue**: the original's effect managers are
    /// the views', which the scene events use too (docs/research/graphics.md).
    void queueScreenEffect(int type, float seconds);

    // ---- The game's frame ----

    /// One update at 30 a second at game time `nowMs`, with `heldButtons` the buttons any player holds: the records
    /// that arrived and their callbacks, then every scene task's update, then the slots of ended scenes freed.
    /// @orig 0x0039cbf0 SceneTask_Update (SceneTask.cpp)
    void update(std::uint64_t nowMs, std::uint16_t heldButtons);

    // ---- Queries ----

    /// Whether any scene task is starting or playing.
    [[nodiscard]] bool playing() const;
    /// Whether a playing cinematic holds the scene state (`0x0051489c + 0x410`): the scene camera governs.
    [[nodiscard]] bool cinematicActive() const;
    /// The state of the scene `id`; Empty when no slot holds it.
    [[nodiscard]] SceneState state(std::uint32_t id) const;
    /// The id `name` finds (first record containing it), nothing when none.
    [[nodiscard]] std::optional<std::uint32_t> idOf(std::string_view name) const;
    /// The frame the scene `id` has reached (from its start, all parts), nothing when it is not playing.
    [[nodiscard]] std::optional<float> frame(std::uint32_t id) const;
    /// The chain-skip flag (`0x0051489c + 0x56e4`): START skipped a scene and the next chained ones skip at once.
    [[nodiscard]] bool chainSkip() const { return m_chainSkip; }
    [[nodiscard]] const SceneStats& stats() const { return m_stats; }
    [[nodiscard]] SceneCache& cache() { return m_cache; }
    [[nodiscard]] const SceneCache& cache() const { return m_cache; }

  private:
    friend class SceneTask;

    // The host, or the defaults.
    [[nodiscard]] SceneHost& host() { return m_host != nullptr ? *m_host : m_defaultHost; }
    // Queues a Lua call for the end of the update (or makes it now outside one).
    void callLua(std::string_view function, std::span<const double> args);
    // The task playing `id`; null when none.
    [[nodiscard]] SceneTask* taskOf(std::uint32_t id) const;
    // Whether `human` is in a starting or playing scene.
    [[nodiscard]] bool inScene(double human) const;

    SceneCache m_cache;
    ScriptCall m_call;
    SceneHost* m_host = nullptr;
    SceneHost m_defaultHost;
    std::vector<std::unique_ptr<SceneTask>> m_tasks;
    std::string m_globalCallback;
    std::uint64_t m_nowMs = 0;
    bool m_chainSkip = false;
    int m_sceneState = 0; // the cinematic step at 0x0051489c + 0x410
    bool m_inUpdate = false;
    std::vector<std::pair<std::string, std::vector<double>>> m_pendingCalls;
    SceneStats m_stats;
};

} // namespace coney::scenes
