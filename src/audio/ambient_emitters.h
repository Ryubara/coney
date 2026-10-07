// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "audio/sound_engine.h"

// The ambient manager's emitters: the level's crickets, dogs, wind, machinery and room loops.
// Research: docs/research/sound.md#ambient

namespace coney::audio {

/// How a level places one ambient emitter (`AddAmbientSoundEmitter2`, or `AddAmbientSoundEmitter` with the name
/// `particle task`; docs/references/bindings/sound.md#addambientsoundemitter2).
struct AmbientEmitterSetup {
    std::string name;           ///< Its name (`+0xad`, 31 characters).
    SoundVec from{};            ///< `pos1` (`+0x60`): the point the range is measured from.
    SoundVec to{};              ///< `pos2` (`+0x10`): the point its sounds play from, until setPositions() gives more.
    int slot = -1;              ///< The first ambient-table slot it plays (`+0x8a`), or -1 for `sound`.
    std::uint32_t sound = 0;    ///< The one sound (`+0x00`) when `slot` is -1.
    std::uint32_t count = 0;    ///< How many table slots from `slot` it plays in turn (`+0x8f`).
    float range = -1.0F;        ///< Metres from `from` (`+0x74`); -1 takes the first sound's far distance + 10.
    int plays = -1;             ///< Plays before it switches itself off (`+0x8c`, a signed byte; -1 without limit).
    std::uint32_t minDelay = 0; ///< The shortest pause between plays, whole seconds (`+0x86`).
    std::uint32_t maxDelay = 0; ///< The longest pause, whole seconds (`+0x88`).
    std::uint8_t mode = 0;      ///< How it plays (`+0x8d`): AmbientEmitters' modes.
    std::uint8_t filter = 0;    ///< Which players hear it (`+0x90`): 0 off covered ground, 1 on it, 2 all.
};

/// What the emitters' update reads of the game.
struct AmbientWorld {
    std::span<const SoundVec> listeners;  ///< The players' listeners: the range is to the nearest.
    std::span<const bool> playersCovered; ///< Per player, whether he stands on covered ground (`+0x5b7`).
    bool scenePlaying = false;            ///< A scene plays (game state `+0x410`): the ambient factor is 0.75.
    bool musicDuck = false;               ///< A music channel is busy and the mood is not 2: `music` emitters duck.
    bool damWindow = false;               ///< The `_DAM_` emitters' window after an AI event is open.
    bool fightTimer = false;              ///< The fight timer is on: `_FHT_` emitters may play, `_DAM_` ones not.
};

/// The ambient manager (audio manager `+0x24280`): the ambient table that `AddAmbientSound` fills (one sound hash per
/// slot) and up to kMaxEmitters emitters, run at most once a second
/// (docs/research/sound.md#ambient). The modes:
/// - **1, 2**: when its delay has passed and nothing plays, it takes one play and plays its next sound at a random
///   one of its points, then draws its next delay;
/// - **3**: a loop at its first point while a listener is in range, stopped beyond it;
/// - **4**: as 1 and 2, but its first play is at once, after which it is mode 2;
/// - **5**: as 1 and 2, but a playing sound is stopped at the next update;
/// - **6**, **7** and others: nothing (7, the conversation emitter no script makes, is not implemented).
///
/// **Coney's stand-ins**: the update runs once for all players (the original runs it once per player, which with one
/// player is the same); the clock is the sound engine's; the `0x005147c8` block is not modelled.
///
/// Game thread only; deterministic for test mode (its draws are the engine's).
class AmbientEmitters {
  public:
    /// The most points an emitter plays from (SetAmbientEmitterPositions).
    static constexpr std::size_t kMaxPositions = 5;
    /// The most emitters the manager holds.
    static constexpr std::size_t kMaxEmitters = 512;
    /// The highest table slot Coney keeps (the original does not check the slot; global.lua's are below 1,100).
    static constexpr int kMaxSlot = 4095;
    /// The update's interval, ms.
    static constexpr double kUpdateMs = 1000.0;
    /// The most sounds the timed modes start in one update, over all emitters.
    static constexpr int kMaxNewPerUpdate = 2;

    /// `AddAmbientSound(slot, sound)`: the table's entry `slot`. A slot out of range is ignored.
    /// @orig 0x0010d3c8 Ambient_SetSound (unknown)
    void setSound(int slot, std::uint32_t hash);
    /// The table's entry `slot` (0 when empty or out of range).
    [[nodiscard]] std::uint32_t sound(int slot) const;

    /// Adds the emitter at the engine's clock (its first delay drawn, its first slot a random one of its count) and
    /// returns its id; an emitter of the same name, sound and first point is switched on instead and its id returned.
    /// -1 when the manager is full (**Coney choice**: the original does not check).
    /// @orig 0x0010cf58 Ambient_AddEmitter (unknown)
    int add(const AmbientEmitterSetup& setup, SoundEngine& engine);
    /// `SetAmbientEmitterPositions`: the points of the first emitter named `name` (at most kMaxPositions are kept);
    /// false when there is no such emitter or no point.
    /// @orig 0x0010d590 Ambient_SetEmitterPositions (unknown)
    bool setPositions(std::string_view name, std::span<const SoundVec> positions);
    /// `EnableAmbientEmitter(id, on)` (`+0x7c`); an id with no emitter is ignored (**Coney choice**: the original does
    /// not check it). Switched off, its sound stops at its next update.
    /// @orig 0x0010d570 AmbientManager_EnableEmitter (unknown)
    void setEnabled(int id, bool on);
    /// Whether emitter `index` is on.
    [[nodiscard]] bool enabled(std::size_t index) const { return m_emitters.at(index).enabled; }
    /// `SetAmbientEmitterVolumeMod(id, volume)`: its volume (`+0x78`), given at once to the sound it plays.
    /// @orig 0x0010d2a0 AmbientEmitter_SetVolume (unknown)
    void setVolume(int id, float volume, SoundEngine& engine);

    /// Runs the emitters, at most once each kUpdateMs of the engine's clock.
    /// @orig 0x0010c100 Ambient_Update (unknown)
    void update(SoundEngine& engine, const AmbientWorld& world);
    /// Stops the emitters' sounds (when there is an engine) and forgets the emitters; the table stays (global.lua
    /// fills it again for each level anyway).
    void clearEmitters(SoundEngine* engine);

    /// The emitters, in the order they were added.
    [[nodiscard]] std::size_t emitterCount() const { return m_emitters.size(); }
    /// The sound emitter `index` plays now (invalid when none).
    [[nodiscard]] SoundHandle playing(std::size_t index) const { return m_emitters.at(index).playing; }
    /// Emitter `index`'s mode now (a mode 4 emitter becomes 2 after its first play).
    [[nodiscard]] std::uint8_t mode(std::size_t index) const { return m_emitters.at(index).mode; }
    /// Emitter `index`'s range, metres.
    [[nodiscard]] float range(std::size_t index) const { return m_emitters.at(index).range; }
    /// How many sounds the emitters have started.
    [[nodiscard]] std::uint64_t plays() const { return m_plays; }

  private:
    // The kind its name gives (`+0xac`).
    enum class Kind : std::uint8_t { Plain, Damage, Fight, Music };
    // One emitter (0xd0 bytes in the original).
    struct Emitter {
        AmbientEmitterSetup setup;
        std::vector<SoundVec> points; // +0x10, +0x8e: pos2, or the positions given
        float range = 0.0F;           // +0x74
        float volume = 1.0F;          // +0x78
        bool enabled = true;          // +0x7c
        int plays = -1;               // +0x8c
        std::uint8_t mode = 0;        // +0x8d
        Kind kind = Kind::Plain;      // +0xac
        int nextSlot = 0;             // +0x94
        double lastMs = 0.0;          // +0x80
        std::int32_t delay = 0;       // +0x84, whole seconds
        SoundHandle playing{};        // +0x70
        std::uint32_t playingHash = 0;
    };
    // The kind of the name `name`.
    [[nodiscard]] static Kind kindOf(std::string_view name);
    // The sound the emitter plays next: its one sound, or its next table slot (in turn, wrapping to its first).
    [[nodiscard]] std::uint32_t nextSound(Emitter& emitter) const;
    // Plays the emitter's next sound at `at` with the ambient `factor`.
    void play(Emitter& emitter, SoundEngine& engine, SoundVec at, float factor);
    // Plays the next sound at a random one of the emitter's points, counting it among the update's new sounds.
    void playTimed(Emitter& emitter, SoundEngine& engine, float factor, int& started);
    // Stops the emitter's sound.
    static void stop(Emitter& emitter, SoundEngine& engine);
    // Draws the next delay from now.
    static void redraw(Emitter& emitter, SoundEngine& engine, double now);
    // Whether the name's kind lets a timed emitter play now.
    [[nodiscard]] static bool kindAllows(const Emitter& emitter, const AmbientWorld& world);
    // Out of range: a playing sound is stopped once the listener is beyond that sound's own far distance.
    static void outOfRange(Emitter& emitter, SoundEngine& engine, float distance);
    // Whether a player the emitter's filter lets hear it is in the game.
    [[nodiscard]] static bool heard(const Emitter& emitter, const AmbientWorld& world);

    std::vector<std::uint32_t> m_table;
    std::vector<Emitter> m_emitters;
    std::uint64_t m_plays = 0;
    double m_lastUpdateMs = 0.0; // +0x04
};

} // namespace coney::audio
