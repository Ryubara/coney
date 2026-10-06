// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <unordered_map>

#include "audio/audio_format.h"
#include "audio/mixer.h"
#include "audio/pcm_sound.h"

namespace coney::audio {

/// A sound as the game names it: the CRC-32 of its name as written (`vags/ambient/beach/foghorn1`). The sound list
/// holds these hashes (inferred in docs/research/sound.md#sound-list: every voice line the game probes for is among
/// them), and the game hashes names with Crc32 without case folding (confirmed (code) at 0x00143f68, same page).
using SoundId = std::uint32_t;

/// The id of the sound named `name` (core's crc32(), no case folding).
/// @orig 0x00143f68 Crc32_Hash (unknown)
/// Research: docs/research/sound.md#original-structure
[[nodiscard]] SoundId soundIdOf(std::string_view name);

/// The game-facing face of the audio: what game code (the HUD, scenes, humans' speech) calls to play a sound by id,
/// stop it and set the bus volumes, on the game thread. It plays the sounds registered with add() through the Mixer;
/// which sound an id stands for comes from the game's banks once their format is researched (docs/research/sound.md),
/// so until then an id with no sound plays nothing and is counted, and callers need not care whether sound is on.
///
/// A caller holds a `SoundPlayer*` that may be null (a run with `--no-audio`); every call is cheap and never blocks.
class SoundPlayer {
  public:
    /// A player over `mixer`, which must outlive it.
    explicit SoundPlayer(Mixer& mixer) : m_mixer(mixer) {}

    /// Makes `id` play `sound` (replacing any earlier one). Voices already playing the old sound play on.
    void add(SoundId id, std::shared_ptr<const PcmSound> sound);
    /// Whether `id` has a sound.
    [[nodiscard]] bool has(SoundId id) const { return m_sounds.contains(id); }

    /// Plays the sound `id` stands for with `params`; an invalid handle (and a count in missing()) when `id` has no
    /// sound, or when the mixer cannot take the command.
    VoiceHandle play(SoundId id, const VoiceParams& params = {});
    /// Plays the sound named `name` (soundIdOf()).
    VoiceHandle play(std::string_view name, const VoiceParams& params = {}) { return play(soundIdOf(name), params); }
    /// Stops a voice play() started.
    void stop(VoiceHandle voice) { m_mixer.stop(voice); }
    /// Whether that voice is still playing.
    [[nodiscard]] bool isPlaying(VoiceHandle voice) const { return m_mixer.isPlaying(voice); }
    /// Sets the volume of `bus` (0 to 1).
    void setBusVolume(Bus bus, float volume) { m_mixer.setBusVolume(bus, volume); }
    /// The volume of `bus`.
    [[nodiscard]] float busVolume(Bus bus) const { return m_mixer.busVolume(bus); }

    /// How many plays asked for an id with no sound.
    [[nodiscard]] std::uint64_t missing() const { return m_missing; }
    /// The mixer underneath, for voice changes (pan, pitch) and streams.
    [[nodiscard]] Mixer& mixer() { return m_mixer; }

  private:
    Mixer& m_mixer;
    std::unordered_map<SoundId, std::shared_ptr<const PcmSound>> m_sounds;
    std::uint64_t m_missing = 0;
};

} // namespace coney::audio
