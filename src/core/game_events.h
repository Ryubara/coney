// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// What the simulation makes a player see or hear, as events a network session can send: a sound, a particle burst,
// a HUD message, a scene or a movie starting (docs/guides/network-architecture.md#checklist, item 8). The simulation
// raises each one through one call, GameEvents::raise(), which keeps it only while a session is open; offline it
// costs one test of a flag and changes nothing the game does. The network layer turns the kept events into packets
// (docs/guides/multiplayer.md#events). Coney's own: the original has no network play.

namespace coney {

/// What happened. Values travel on the wire (docs/guides/multiplayer.md#events) and are never reused.
enum class GameEventKind : std::uint8_t {
    Sound2D = 1,       ///< A 2D sound: `id` its name hash.
    Sound3D = 2,       ///< A positional sound: `id` its name hash, at `position`.
    HumanSound = 3,    ///< A human's sound: `subject` him, `value` what (0 his animation sound, `id` its `SA` id; 1 the
                       ///< held sound's stop, clip event `0x45`; 2 rage's start, the game's rage sound), at
                       ///< `position` (his feet).
    Hit = 4,           ///< A hit's impact sound: `subject` the human it sounds at, `id` the striking material, `value`
                       ///< the struck one, at `position`.
    MusicStart = 5,    ///< A music track: `id` its hash, `value` bit 0 looping, bit 1 system music, `extra` its fade
                       ///< in bars (system music only).
    MusicStop = 6,     ///< The music stops.
    LoopStart = 7,     ///< The ambient track (a loop): `id` its hash.
    LoopStop = 8,      ///< The ambient track stops.
    ParticleStart = 9, ///< A particle system: `text` its type, at `position` turned by `rotation`, following
                       ///< `subject` (none: 0), `id` its colour and `value` its rectangle (the type's unless its
                       ///< creator gave others).
    ObjectBreak = 10,  ///< A world object breaks: `subject` it, `id` how (a car's hit effect kind plus 1, 0 a prop),
                       ///< at `position`.
    HudMessage = 11,   ///< An announcement: `id` its kind, `value` its flag, `text` it.
    Objective = 12,    ///< An objective: `id` its slot, `value` its mode shifted left by 1 with bit 0 silent, `extra`
                       ///< its time in milliseconds, `text` it.
    SceneStart = 13,   ///< A scene starts: `id` the scene.
    SceneEnd = 14,     ///< A scene ends: `id` the scene.
    MovieStart = 15,   ///< A movie starts: `text` its name, `value` 1 when skippable.
    MovieEnd = 16,     ///< A movie ends: `text` its name, `value` 1 when a button skipped it.
    Speech = 17,       ///< A line a human says by name (`HuSpeak`): `subject` him, `text` the line, `value` 1 when it
                       ///< cuts off the line he was saying (`HuSpeakNI`).
    Command = 18,      ///< A speech command a human says: `subject` him, `id` the command, `value` his voice set plus
                       ///< 1 (0 unknown), `extra` bit 0 interrupting, bit 1 cutting.
    SpeechStop = 19,   ///< A human's line is stopped (`HuShutUp`): `subject` him, `value` 1 when forced.
    MaterialPair = 20, ///< A world object's material pair sound from the sound matrix: `id` its material, `value` the
                       ///< other, `extra` the volume in thousandths, at `position`.
    Cue = 21,          ///< An interface cue: `id` the cue, `subject` the human whose HUD plays it (0 none), `value` 1
                       ///< when it plays at `position` (a world object's), 0 as a 2D sound.
    Hint = 22,         ///< A tutorial hint (`HUDSetTutorialText`, `HUDFlushTutorialText`,
                       ///< `HUDEnableGameTutorialText`): `id` 0 queued (`text` it, `value` its priority), 1 the
                       ///< showing hint cleared, 2 flushed (`value` the priority), 3 the game's own hints on or off
                       ///< (`value` 1 on).
};

/// The first and last GameEventKind, for loops and range checks.
inline constexpr GameEventKind kFirstGameEventKind = GameEventKind::Sound2D;
inline constexpr GameEventKind kLastGameEventKind = GameEventKind::MovieEnd;

/// The longest `text` an event carries, in bytes: a HUD message's text with its markup fits.
inline constexpr std::size_t kMaxGameEventText = 255;

/// The fields a kind of event carries; the others stay at their defaults (GameEventFields bits).
enum GameEventField : std::uint8_t {
    kEventSubject = 1U << 0U,
    kEventId = 1U << 1U,
    kEventValue = 1U << 2U,
    kEventExtra = 1U << 3U,
    kEventPosition = 1U << 4U,
    kEventText = 1U << 5U,
    kEventRotation = 1U << 6U,
};

/// One event as the simulation raises it. Whom it names is a script handle (a human's or a world object's), never a
/// pointer: the network layer maps it to that thing's network id.
struct GameEvent {
    GameEventKind kind = GameEventKind::Sound2D;
    double subject = 0.0;                                  ///< The human or object it belongs to; 0 for none.
    std::uint32_t id = 0;                                  ///< What: a sound's hash, a scene, a slot (by kind).
    std::uint32_t value = 0;                               ///< A small number or flags (by kind).
    std::uint32_t extra = 0;                               ///< A second number (by kind).
    std::array<float, 3> position{};                       ///< Where, game axes, metres.
    std::string text{};                                    ///< A name or a message, at most kMaxGameEventText bytes.
    std::array<float, 4> rotation{0.0F, 0.0F, 0.0F, 1.0F}; ///< Which way it faces: a quaternion (x, y, z, w).

    friend bool operator==(const GameEvent&, const GameEvent&) = default;
};

/// Whether `kind` is a valid GameEventKind.
[[nodiscard]] bool validGameEventKind(std::uint8_t kind);
/// The fields `kind` carries (GameEventField bits).
[[nodiscard]] std::uint8_t gameEventFields(GameEventKind kind);
/// Whether events of `kind` must arrive (music, loops, messages, objectives, scenes, movies) rather than being
/// one-shots a lost packet may drop (sounds, hits, particles, breaks).
[[nodiscard]] bool reliableGameEvent(GameEventKind kind);
/// The kind's short name, for logs and record lines: `sound2d`, `hit`, `scene_start`, ...
[[nodiscard]] std::string_view describe(GameEventKind kind);
/// One line of text for an event: its kind's name, then each field it carries as `name=value` (`id` and `extra` in hex,
/// `subject` and `value` in decimal, the position to the millimetre, the text quoted). For logs and session records.
[[nodiscard]] std::string describe(const GameEvent& event);

/// The one call through which the simulation reports what a player sees or hears (the checklist's item 8). It keeps
/// the events raised while recording, which network play turns on while it hosts; otherwise raise() drops them, so
/// one-player play is unchanged. Owned by the run (platform::NetPlay), outside every game mode, and handed to the
/// subsystems that raise events by pointer; a null pointer raises nothing.
///
/// Simulation state's side channel in the sense of docs/guides/network-architecture.md#checklist: written during a
/// step, read by network play after it.
class GameEvents {
  public:
    /// Starts or stops keeping events; either way the kept ones are dropped.
    void setRecording(bool on) {
        m_recording = on;
        m_raised.clear();
    }
    /// Whether raise() keeps events.
    [[nodiscard]] bool recording() const { return m_recording; }
    /// Keeps `event` when recording; a text over kMaxGameEventText bytes is cut to it.
    void raise(GameEvent event);
    /// The events raised since the last call, in the order they were raised.
    [[nodiscard]] std::vector<GameEvent> take();
    /// How many are kept now.
    [[nodiscard]] std::size_t size() const { return m_raised.size(); }

  private:
    bool m_recording = false;
    std::vector<GameEvent> m_raised;
};

/// Raises the scenes' changes from `before` to `now` (each a list of scene ids in rising order, as
/// scenes::SceneSystem::playingScenes() gives): a SceneEnd for each id that has gone, then a SceneStart for each that
/// is new; then `before` becomes `now`. The scenes are compared a step apart, so a scene that starts and ends within
/// one step raises nothing (a scene runs for frames).
void raiseSceneChanges(GameEvents* events, std::vector<std::uint32_t>& before, std::vector<std::uint32_t> now);

/// Raises `event` on `events` when there is one: the form the subsystems use, as they hold a pointer that may be null.
inline void raiseGameEvent(GameEvents* events, GameEvent event) {
    if (events != nullptr && events->recording()) {
        events->raise(std::move(event));
    }
}

} // namespace coney
