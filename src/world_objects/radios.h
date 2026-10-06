// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/game_random.h"

namespace coney::world_objects {

/// The sounds a radio plays: streamed sounds of the sound list started at the radio (`PlaySound3D`), by their hash.
/// The gameplay gives Radios one over the game's sound; a test gives its own.
class RadioSound {
  public:
    RadioSound() = default;
    RadioSound(const RadioSound&) = delete;
    RadioSound& operator=(const RadioSound&) = delete;
    RadioSound(RadioSound&&) = delete;
    RadioSound& operator=(RadioSound&&) = delete;
    virtual ~RadioSound() = default;

    /// Starts the sound `hash` at `position` (volume and pitch 1); a handle, 0 when nothing plays.
    virtual double play(std::uint32_t hash, const std::array<float, 3>& position) = 0;
    /// Whether the sound `handle` still plays.
    [[nodiscard]] virtual bool playing(double handle) const = 0;
    /// Stops it.
    virtual void stop(double handle) = 0;
    /// Moves it to `position` and sets its volume.
    virtual void follow(double handle, const std::array<float, 3>& position, float volume) = 0;
};

/// What a radio's update needs to know from the world.
struct RadioWorld {
    /// Where player 1 is; nothing when there is none.
    std::optional<std::array<float, 3>> player;
    /// Player 1's handle, for `onPickUp`.
    double playerHandle = 0;
    /// Whether a scene plays (`0x0051489c` `+0x410`).
    bool scene = false;
    /// Whether the story level `level` is complete (its unlockable, `0x004241d8(0x006fe998, level)`).
    std::function<bool(int level)> levelComplete;
};

/// One radio (`SetupRadio`): the record Coney keeps of the original's (docs/research/sound.md#radios).
struct Radio {
    double object = 0;              ///< The boom box.
    double sound = 0;               ///< `+0x24`: the sound playing, 0 for none.
    int track = 0;                  ///< `+0x28`: the current track, or the clip's index while a clip plays.
    int next = 0;                   ///< `+0x2c`: the next track, or the armed announcement.
    int state = 1;                  ///< `+0x30`.
    int clipKind = 0;               ///< `+0x34`: 0, 1, 2 the DJ link kinds, 3 the announcements.
    bool announcementArmed = false; ///< `+0x36`.
    std::string onPickUp;           ///< `+0x38`.
    std::string onSegment;          ///< `+0x3c`.
};

/// The level's radios: `SetupRadio` makes one of a world object, and each frame Radios::update() runs each one's
/// states, playing its tracks, DJ links and announcements at the radio while player 1 is within 40 m.
///
/// **Coney's stand-ins** where the page is silent: a radio updates every frame; a track or clip stopped because the
/// player went beyond 40 m starts again from the start (state 1) when he comes back; the DJ links of kinds 0 and 1,
/// whose sounds are not named, play nothing, so they end at once; Radio_SetMode's modes 4 and up (never used) are
/// not built, nor the messages that pick the radio up, smash it or switch it (Coney has no carried boom box).
///
/// Research: docs/research/sound.md#radios, docs/references/bindings/sound.md#setupradio
class Radios {
  public:
    /// The player hears a radio within this many metres.
    static constexpr float kHearing = 40.0F;
    /// An armed announcement starts when the player comes within this many metres.
    static constexpr float kAnnounce = 5.0F;
    /// A track's volume while a scene plays.
    static constexpr float kSceneVolume = 0.5F;

    /// The states (`+0x30`).
    static constexpr int kStart = 1;
    static constexpr int kTrack = 2;
    static constexpr int kRetune = 3;
    static constexpr int kRetuning = 4;
    static constexpr int kClip = 5;
    static constexpr int kClipPlaying = 6;
    static constexpr int kRetuned = 7;
    static constexpr int kNextTrack = 8;
    static constexpr int kSwitchOff = 9;
    static constexpr int kOff = 10;
    static constexpr int kPause = 11;
    static constexpr int kPaused = 12;

    /// The track table's hashes (`0x00512cd8`, 21 entries).
    [[nodiscard]] static const std::array<std::uint32_t, 21>& tracks();
    /// The clip tables' hashes by kind (0-2 the DJ links, 3 the announcements); 0 for a sound with no known name.
    [[nodiscard]] static std::uint32_t clip(int kind, int index);

    /// `SetupRadio(object, onPickUp, track, onSegment, djLine)`: the object made a radio (one already a radio is set up
    /// again).
    /// @orig 0x003ac4d0 Radio_Setup (unknown)
    void setup(double object, std::string onPickUp, int track, std::string onSegment, int djLine);
    /// `Radio_SetMode(mode)`: 1 retunes a radio playing (states 1, 2, 11, 12); 0 and 2 switch it off; 3 pauses it.
    /// The sound stops whenever the state changes.
    /// @orig 0x003ac600 Radio_SetMode (unknown)
    void setMode(double object, int mode, RadioSound& sound);

    /// One update of every radio: `locate` finds a radio's object (nothing: it is skipped), `world` the player and
    /// the progress, `call` calls a script function (with the player's handle for `onPickUp`, none for
    /// `onSegment`).
    /// @orig 0x003ad440 Radio_Update (unknown)
    void update(const std::function<std::optional<std::array<float, 3>>(double object)>& locate,
                const RadioWorld& world, RadioSound& sound, GameRandom& random,
                const std::function<void(const std::string& function)>& call);

    /// The radio of `object`, or null.
    [[nodiscard]] const Radio* find(double object) const;
    /// Every radio.
    [[nodiscard]] const std::vector<Radio>& all() const { return m_radios; }
    /// Forgets every radio: the level is unloaded.
    void clear() { m_radios.clear(); }

  private:
    // One radio's update.
    void updateOne(Radio& radio, const std::array<float, 3>& at, const RadioWorld& world, RadioSound& sound,
                   GameRandom& random, const std::function<void(const std::string& function)>& call);

    std::vector<Radio> m_radios;
};

} // namespace coney::world_objects
