// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "audio/sound_engine.h"

namespace coney::audio {

/// How a level places one ambient emitter (`AddAmbientSoundEmitter2`, docs/references/bindings/sound.md).
struct AmbientEmitterSetup {
    std::string name;           ///< Its name; adding a name already used changes that emitter.
    SoundVec from{};            ///< One end of the line its sounds come from.
    SoundVec to{};              ///< The other end (the same point for a point source).
    int slot = -1;              ///< The first ambient-table entry it picks from, or -1 for `sound`.
    std::uint32_t sound = 0;    ///< The one sound it plays when `slot` is -1.
    std::uint32_t count = 0;    ///< How many table entries from `slot` it picks from.
    float range = -1.0F;        ///< Kept; inferred to be the audible range (not read: an open item).
    std::uint32_t minDelay = 0; ///< The shortest pause between plays, seconds (inferred).
    std::uint32_t maxDelay = 0; ///< The longest pause, seconds (inferred).
};

/// The ambient manager: the ambient table that `AddAmbientSound` fills (one sound hash per slot) and the level's
/// emitters, each playing, at random intervals, a random sound of its slots (or its one sound) somewhere along its line
/// or at one of its positions (docs/research/sound.md#ambient).
///
/// The table store is confirmed (code); the emitters' timing is not traced. **Coney's stand-ins**, each marked where
/// it is made: an emitter plays one sound at a time and waits a random whole number of seconds in [minDelay, maxDelay]
/// before the first and after each one ends (a looping sound plays on); a sound comes from a uniformly random point of
/// the line from `from` to `to`, or from a random one of the positions SetAmbientEmitterPositions gave; `range`, the
/// special types some names get, `arg8`, `arg11` and `mode` are not read.
///
/// Game thread only; deterministic for test mode (its draws are the engine's).
class AmbientEmitters {
  public:
    /// The most positions an emitter takes (SetAmbientEmitterPositions).
    static constexpr std::size_t kMaxPositions = 5;
    /// The highest table slot Coney keeps (the original does not check the slot; global.lua's are below 1,100).
    static constexpr int kMaxSlot = 4095;

    /// `AddAmbientSound(slot, sound)`: the table's entry `slot`. A slot out of range is ignored.
    /// @orig 0x0010d3c8 Ambient_SetSound (unknown)
    void setSound(int slot, std::uint32_t hash);
    /// The table's entry `slot` (0 when empty or out of range).
    [[nodiscard]] std::uint32_t sound(int slot) const;

    /// `AddAmbientSoundEmitter2`: adds the emitter, or changes the one of that name; returns its id (its index).
    /// @orig 0x0010cf58 Ambient_AddEmitter (unknown)
    int add(const AmbientEmitterSetup& setup);
    /// `SetAmbientEmitterPositions`: the positions of the emitter `name` (at most kMaxPositions are kept); false when
    /// there is no such emitter.
    /// @orig 0x0010d590 Ambient_SetEmitterPositions (unknown)
    bool setPositions(std::string_view name, std::span<const SoundVec> positions);

    /// Runs each emitter at the engine's clock: an emitter whose sound has ended waits its pause, then plays its next.
    /// @orig 0x0010c100 Ambient_Update (unknown)
    void update(SoundEngine& engine);
    /// Stops the emitters' sounds (when there is an engine) and forgets the emitters; the table stays (global.lua
    /// fills it again for each level anyway).
    void clearEmitters(SoundEngine* engine);

    /// The emitters, in the order they were added.
    [[nodiscard]] std::size_t emitterCount() const { return m_emitters.size(); }
    /// The sound emitter `index` plays now (invalid when none).
    [[nodiscard]] SoundHandle playing(std::size_t index) const { return m_emitters.at(index).playing; }
    /// How many sounds the emitters have started.
    [[nodiscard]] std::uint64_t plays() const { return m_plays; }

  private:
    // One emitter: its setup and where it is in its cycle.
    struct Emitter {
        AmbientEmitterSetup setup;
        std::vector<SoundVec> positions;
        SoundHandle playing;
        bool waiting = false; // the pause before the next sound is running
        double nextMs = 0.0;
    };
    // The hash the emitter plays next (0: none).
    [[nodiscard]] std::uint32_t pickSound(const Emitter& emitter, SoundEngine& engine) const;
    // Where the emitter's next sound comes from.
    [[nodiscard]] static SoundVec pickPosition(const Emitter& emitter, SoundEngine& engine);

    std::vector<std::uint32_t> m_table;
    std::vector<Emitter> m_emitters;
    std::uint64_t m_plays = 0;
};

} // namespace coney::audio
