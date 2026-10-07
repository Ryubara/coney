// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "audio/sound_engine.h"
#include "audio/sound_matrix.h"

namespace coney::audio {

/// Where the game's sounds go: the engine in a run, a recorder in a test (no real audio needed).
class SoundSink {
  public:
    virtual ~SoundSink() = default;
    SoundSink() = default;
    SoundSink(const SoundSink&) = delete;
    SoundSink& operator=(const SoundSink&) = delete;
    SoundSink(SoundSink&&) = delete;
    SoundSink& operator=(SoundSink&&) = delete;

    /// Starts the sound `hash` as `how` says (SoundEngine::play()); its handle, invalid when nothing plays.
    virtual SoundHandle play(std::uint32_t hash, const SoundPlay& how) = 0;
    /// Stops a sound play() started (nothing for one that ended).
    virtual void stop(SoundHandle /*sound*/) {}
};

/// The sound matrix's players (docs/research/sound.md#sound-matrix): what contacts, objects, footsteps and the
/// animations' sound events play. Each takes the matrix's next alternative and starts its sounds at a position through
/// a SoundSink; a column with no sound plays nothing.
///
/// Research: docs/research/sound.md#sound-matrix
class MaterialSoundPlayer {
  public:
    /// The material most callers default to when the second material is none: 5, `CONCRETE`.
    static constexpr std::uint32_t kDefaultMaterial = 5;
    /// `CAR_HOOD`: a pair whose first material is this plays its second sound at a varied pitch.
    static constexpr std::uint32_t kCarHoodMaterial = 13;

    /// A footstep's material after the footstep caller's remap, and the volume factor it brings.
    struct FootMaterial {
        std::uint32_t material = 0;
        float volume = 1.0F;
    };

    /// Players over `matrix` (which must outlive them) into `sink` (null: nothing plays; set it later).
    explicit MaterialSoundPlayer(SoundMatrix& matrix, SoundSink* sink = nullptr) : m_matrix(matrix), m_sink(sink) {}

    /// Plays into `sink` from now on (null: nothing).
    void setSink(SoundSink* sink) { m_sink = sink; }
    /// The matrix it plays from.
    [[nodiscard]] SoundMatrix& matrix() { return m_matrix; }

    /// A material pair's sounds at `at`: column 2 then column 1 of the pair's next alternative, each at `volume` × its
    /// column's volume. When `a` is `CAR_HOOD` the second plays at a varied pitch (**Coney stand-in**: at pitch 1; the
    /// factor `0x003354e0() × 0.2` is open on docs/research/sound.md#sound-matrix).
    /// @orig 0x00110830 Sound_PlayMaterialPair (unknown)
    void playMaterialPair(float volume, std::uint32_t a, std::uint32_t b, SoundVec at,
                          std::uint32_t fallback = kDefaultMaterial, std::uint32_t owner = 0);
    /// The same with no pitch variation (the footsteps').
    /// @orig 0x00110940 Sound_PlayMaterialPairPlain (unknown)
    void playMaterialPairPlain(float volume, std::uint32_t a, std::uint32_t b, SoundVec at,
                               std::uint32_t fallback = kDefaultMaterial, std::uint32_t owner = 0);
    /// `playMaterialPair(1.0, a, b, at, 5)`: the glass panes' and other objects' contacts.
    /// @orig 0x00117280 Sound_PlayMaterialPairAt (unknown)
    void playMaterialPairAt(std::uint32_t a, std::uint32_t b, SoundVec at) { playMaterialPair(1.0F, a, b, at); }
    /// Column 1 only of the pair's next alternative, at `volume` × its volume.
    /// @orig 0x00110790 Sound_PlayMaterialHit (unknown)
    void playMaterialHit(float volume, std::uint32_t a, std::uint32_t b, SoundVec at,
                         std::uint32_t fallback = kDefaultMaterial, std::uint32_t owner = 0);
    /// Column 1 of animation sound event `event`'s next alternative at `at`, at `volume` × its volume.
    /// @orig 0x00110a08 Sound_PlayAnimSound (unknown)
    void playAnimSound(float volume, std::uint32_t event, SoundVec at, std::uint32_t owner = 0);

    /// The footstep caller's remap of the ground's material: `0x12` (TRAINRAIL) -> `0x6a`, 6 (ASHPHALT) -> 5, `0x79`
    /// -> `0x23` at half volume; any other unchanged at full volume.
    /// @orig 0x00110aa8 Sound_RemapFootMaterial (unknown)
    [[nodiscard]] static FootMaterial remapFootMaterial(std::uint32_t ground);

    /// One sound (none for hash 0) at `at`, positional and duckable, owned by `owner` (0 none); its handle.
    SoundHandle playSound(std::uint32_t hash, float volume, float pitch, SoundVec at, std::uint32_t owner = 0);
    /// Stops a sound playSound() started.
    void stop(SoundHandle sound) {
        if (m_sink != nullptr) {
            m_sink->stop(sound);
        }
    }

    /// Sounds started (counting each column), for tests and logs.
    [[nodiscard]] std::uint64_t started() const { return m_started; }
    /// Lookups that found no entry.
    [[nodiscard]] std::uint64_t unmatched() const { return m_unmatched; }

  private:
    // The pair's two columns, column 2 first, the second at `secondPitch`.
    void playPair(float volume, std::uint32_t a, std::uint32_t b, SoundVec at, std::uint32_t fallback,
                  std::uint32_t owner, float secondPitch);

    SoundMatrix& m_matrix;
    SoundSink* m_sink;
    std::uint64_t m_started = 0;
    std::uint64_t m_unmatched = 0;
};

} // namespace coney::audio
