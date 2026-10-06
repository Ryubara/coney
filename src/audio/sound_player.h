// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "audio/audio_format.h"
#include "audio/mixer.h"
#include "audio/pcm_sound.h"
#include "audio/sound_engine.h"

namespace coney::audio {

/// A sound as the game names it: the CRC-32 of its name as written (`vags/ambient/beach/foghorn1`). The sound list
/// holds these hashes (docs/research/formats/audio.md#sound-list), and the game hashes names with Crc32 without case
/// folding (confirmed (code) at 0x00143f68, docs/research/sound.md#original-structure).
using SoundId = std::uint32_t;

/// The id of the sound named `name` (core's crc32(), no case folding).
/// @orig 0x00143f68 Crc32_Hash (unknown)
/// Research: docs/research/sound.md#original-structure
[[nodiscard]] SoundId soundIdOf(std::string_view name);

/// The game-facing face of the audio: what game code (the HUD, scenes, menus, humans' speech) calls to play a sound by
/// id, stop it and set the volumes, on the game thread. An id plays, in order: a sound registered with add() (Coney's
/// own, such as the debug menu's test tone), else the game's sound through the SoundEngine (the sound list, the loaded
/// bank and the BFW.SND streams) once one is attached; an id with neither plays nothing and is counted. Banks, music,
/// the load screen and scene soundtracks are the engine's: engine().
///
/// A caller holds a `SoundPlayer*` that may be null (a run with `--no-audio`); every call is cheap and never blocks.
/// The handles play() returns are the player's own: they stay harmless once their sound has ended.
class SoundPlayer {
  public:
    /// A player over `mixer`, which must outlive it.
    explicit SoundPlayer(Mixer& mixer) : m_mixer(mixer) {}

    /// Gives the player the game's sound engine (made over the same mixer); null takes it away.
    void attach(std::unique_ptr<SoundEngine> engine) { m_engine = std::move(engine); }
    /// The game's sound engine, or null when there is none (no disc, or its sound data could not be read).
    [[nodiscard]] SoundEngine* engine() { return m_engine.get(); }
    [[nodiscard]] const SoundEngine* engine() const { return m_engine.get(); }

    /// Makes `id` play `sound` (replacing any earlier one). Voices already playing the old sound play on.
    void add(SoundId id, std::shared_ptr<const PcmSound> sound);
    /// Whether `id` has a sound: registered, or in the engine's sound list.
    [[nodiscard]] bool has(SoundId id) const;

    /// Plays the sound `id` stands for as a 2D sound with `params` (its volume, pan and pitch; the bus and priority
    /// only for a registered sound); an invalid handle (and a count in missing()) when `id` has no sound, or when it
    /// cannot play.
    VoiceHandle play(SoundId id, const VoiceParams& params = {});
    /// Plays the sound named `name` (soundIdOf()).
    VoiceHandle play(std::string_view name, const VoiceParams& params = {}) { return play(soundIdOf(name), params); }
    /// Plays the game's sound `id` at `position` (a positional sound; one whose class has no position plays as 2D).
    VoiceHandle play3D(SoundId id, SoundVec position, const VoiceParams& params = {});
    /// Stops a sound play() started, at once or fading out over `fadeOutMs` (the game's sounds only).
    void stop(VoiceHandle sound, float fadeOutMs = 0.0F);
    /// Moves a positional sound.
    void setPosition(VoiceHandle sound, SoundVec position);
    /// Sets a sound's volume (its caller volume, 0 to 1).
    void setVolume(VoiceHandle sound, float volume);
    /// Pauses every sound playing now, music included; sounds played afterwards play (SoundEngine::pause(),
    /// Mixer::pauseAll()).
    void pauseAll();
    /// Resumes the sounds pauseAll() paused.
    void resumeAll();
    /// Whether that sound is still playing.
    [[nodiscard]] bool isPlaying(VoiceHandle sound) const;
    /// Sets the volume of `bus` (0 to 1).
    void setBusVolume(Bus bus, float volume) { m_mixer.setBusVolume(bus, volume); }
    /// The volume of `bus`.
    [[nodiscard]] float busVolume(Bus bus) const { return m_mixer.busVolume(bus); }

    /// Where the sound is heard from: one listener, or two (one per player). Kept until changed; one at the origin to
    /// start with.
    void setListeners(std::span<const Listener> listeners);
    /// Runs the engine for `milliseconds` of game time (each step's 1/30 s) and forgets ended handles. Call once a
    /// frame with the steps it ran.
    void update(float milliseconds);

    /// How many plays asked for an id with no sound.
    [[nodiscard]] std::uint64_t missing() const { return m_missing; }
    /// The mixer underneath, for voice changes (pan, pitch) and streams.
    [[nodiscard]] Mixer& mixer() { return m_mixer; }

  private:
    // What a player handle stands for: a mixer voice (a registered sound) or an engine sound.
    struct Played {
        bool engine = false;
        std::uint32_t id = 0;
    };
    VoiceHandle remember(Played played);
    [[nodiscard]] const Played* lookup(VoiceHandle sound) const;
    [[nodiscard]] bool playing(const Played& played) const;
    [[nodiscard]] static SoundPlay engineParams(const VoiceParams& params);

    Mixer& m_mixer;
    std::unique_ptr<SoundEngine> m_engine;
    std::unordered_map<SoundId, std::shared_ptr<const PcmSound>> m_sounds;
    std::unordered_map<std::uint32_t, Played> m_played;
    std::uint32_t m_nextHandle = 1;
    std::vector<Listener> m_listeners{Listener{}};
    std::uint64_t m_missing = 0;
};

} // namespace coney::audio
