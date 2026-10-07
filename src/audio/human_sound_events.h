// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <map>

#include "audio/material_sounds.h"
#include "audio/sound_engine.h"
#include "scripting/sound_bindings.h"

namespace coney::audio {

/// What the humans' sounds read of a human's character type (its `CfgChar` record).
struct HumanTraits {
    bool female = false;       ///< `+0x3b8` (`CfgChar`'s 13th argument): the vocal ids' `_female` entries.
    bool bossClass = false;    ///< Class `+0x11b` 13: the bosses and big fighters.
    bool canSpeak = true;      ///< `+0x199`: he may say lines (`HuEnableSoundCommands`).
    float combatFactor = 1.0F; ///< `CfgBreathingSound`'s first argument (game state `+0x24c`), under combat framing.
};

/// The speech side of the humans' sounds: the game's sound in a run, a recorder in a test.
class HumanVoices {
  public:
    virtual ~HumanVoices() = default;
    HumanVoices() = default;
    HumanVoices(const HumanVoices&) = delete;
    HumanVoices& operator=(const HumanVoices&) = delete;
    HumanVoices(HumanVoices&&) = delete;
    HumanVoices& operator=(HumanVoices&&) = delete;

    /// Whether `human` is saying a line (`+0x178` alive).
    [[nodiscard]] virtual bool speaking(double human) const = 0;
    /// Stops the line `human` is saying.
    virtual void stopLine(double human) = 0;
    /// Plays the sound `hash` as `who`'s speech line at `volume` (cutting: `HuSpeakNI`'s kind); whether it plays.
    /// Nothing plays during a cinematic.
    virtual bool sayLine(const script::HumanSoundCall& who, std::uint32_t hash, float volume, bool cut) = 0;
    /// `who` says speech command `command` (`Human_SayCommand`) at `volume`; whether a line came.
    virtual bool sayCommand(const script::HumanSoundCall& who, std::uint32_t command, float volume, bool interrupt,
                            bool duckable) = 0;
    /// `Ambient_MayGesture`: whether `who` may make a gesture line now (taking a slot when he may).
    virtual bool mayGesture(const script::HumanSoundCall& who) = 0;
    /// Whether a scene plays (game state `+0x410`).
    [[nodiscard]] virtual bool scenePlaying() const = 0;
};

/// The humans' animation and hit sounds (docs/research/sound-events.md): `Human_OnAnimSoundEvent`'s table of the 162
/// animation sound ids (footsteps and body falls, fixed pairs, held objects, sounds without a speaker, vocal lines,
/// speech commands) and `Human_PlayImpactSound`, over the sound matrix's players and a HumanVoices.
///
/// **Coney's stand-ins** (each open in Coney, not in the research): no human carries a world object, so the held-object
/// ids (52, 86) play nothing and 62 plays its own entry; the grab sound prepared for `uncuff` (60) and the stealth ids'
/// hidden loop (87-89) are not built, so 60 plays its entry and 87-89 their line; `cmd_throw`'s level-31 lines are not
/// built.
///
/// Research: docs/research/sound-events.md
class HumanSoundEvents {
  public:
    /// Draws a whole number in [low, high] (the audio generator).
    using RandomRange = std::function<std::int32_t(std::int32_t low, std::int32_t high)>;

    /// The events over `sounds` and `voices` (both must outlive it), drawing from `random`.
    HumanSoundEvents(MaterialSoundPlayer& sounds, HumanVoices& voices, RandomRange random);

    /// Plays what `call` asks for, for a human of `traits`.
    void play(const script::HumanSoundCall& call, const HumanTraits& traits);

    /// The animation sound `id` of `who` (human message `0x8b`).
    /// @orig 0x0021f700 Human_OnAnimSoundEvent (unknown)
    void animSound(const script::HumanSoundCall& who, const HumanTraits& traits, std::uint32_t id);
    /// The material pair (m1, m2) at `who` (the owner), at `volume`; `victimDown` turns TORSO and HEAD on either side
    /// into TORSO_PRONE.
    /// @orig 0x00220ac8 Human_PlayImpactSound (unknown)
    void impact(const script::HumanSoundCall& who, const HumanTraits& traits, float volume, std::uint32_t m1,
                std::uint32_t m2, bool victimDown, SoundVec at);

  private:
    // The animation entry `id` at the human, owned by him: a player's columns 1 and 2 (and 3 under combat framing)
    // twice as loud; anyone else's column 1. Whether a sound came.
    // @orig 0x0021f548 Human_PlayAnimSound (unknown)
    bool playAnimSound(const script::HumanSoundCall& who, const HumanTraits& traits, std::uint32_t id);
    // A footstep or body fall: the body material against the ground under him.
    // @orig 0x0021f290 Human_PlayFootstep (unknown)
    void footstep(const script::HumanSoundCall& who, const HumanTraits& traits, float volume, std::uint32_t body);
    // An animation entry's column 1 said as his line (`femaleId` for a woman when not 0); whether it played.
    // @orig 0x0021f410 Human_SayAnimLine (unknown)
    bool sayAnimLine(const script::HumanSoundCall& who, const HumanTraits& traits, std::uint32_t id,
                     std::uint32_t femaleId, bool overLine, bool cut);
    // The combat factor for `who`: the configured factor for a player under combat framing, else 1.
    // @orig 0x0021e3a8 Human_GetSoundVolumeScale (unknown)
    [[nodiscard]] static float combatFactor(const script::HumanSoundCall& who, const HumanTraits& traits);
    // The held object's material against `other` (itself when 0): a player's pair, anyone else's hit.
    void heldContact(const script::HumanSoundCall& who, const HumanTraits& traits, std::uint32_t other);

    MaterialSoundPlayer& m_sounds;
    HumanVoices& m_voices;
    RandomRange m_random;
    std::map<double, SoundHandle> m_loops; // SA 83's sound per human (+0x168)
};

} // namespace coney::audio
