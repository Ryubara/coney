// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/material_sounds.h"

namespace coney::audio {

namespace {

// The footstep remap's pairs (docs/research/sound.md#sound-matrix).
constexpr std::uint32_t kTrainRail = 0x12;
constexpr std::uint32_t kTrainRailFoot = 0x6a;
constexpr std::uint32_t kAsphalt = 6;
constexpr std::uint32_t kConcrete = 5;
constexpr std::uint32_t kHalfVolumeGround = 0x79;
constexpr std::uint32_t kHalfVolumeFoot = 0x23;

} // namespace

SoundHandle MaterialSoundPlayer::playSound(std::uint32_t hash, float volume, float pitch, SoundVec at,
                                           std::uint32_t owner) {
    if (hash == 0 || m_sink == nullptr) {
        return {};
    }
    SoundPlay how;
    how.volume = volume;
    how.pitch = pitch;
    how.owner = owner;
    how.position = at;
    ++m_started;
    return m_sink->play(hash, how);
}

void MaterialSoundPlayer::playPair(float volume, std::uint32_t a, std::uint32_t b, SoundVec at, std::uint32_t fallback,
                                   std::uint32_t owner, float secondPitch) {
    const std::optional<MatrixSounds> sounds = m_matrix.nextMaterialSounds(a, b, fallback);
    if (!sounds) {
        ++m_unmatched;
        return;
    }
    // Column 2 first, then column 1.
    static_cast<void>(playSound(sounds->sounds[1], volume * sounds->volumes[1], 1.0F, at, owner));
    static_cast<void>(playSound(sounds->sounds[0], volume * sounds->volumes[0], secondPitch, at, owner));
}

void MaterialSoundPlayer::playMaterialPair(float volume, std::uint32_t a, std::uint32_t b, SoundVec at,
                                           std::uint32_t fallback, std::uint32_t owner) {
    // Coney stand-in: CAR_HOOD's varied pitch is open (docs/research/sound.md#sound-matrix); every pair at pitch 1.
    playPair(volume, a, b, at, fallback, owner, 1.0F);
}

void MaterialSoundPlayer::playMaterialPairPlain(float volume, std::uint32_t a, std::uint32_t b, SoundVec at,
                                                std::uint32_t fallback, std::uint32_t owner) {
    playPair(volume, a, b, at, fallback, owner, 1.0F);
}

void MaterialSoundPlayer::playMaterialHit(float volume, std::uint32_t a, std::uint32_t b, SoundVec at,
                                          std::uint32_t fallback, std::uint32_t owner) {
    const std::optional<MatrixSounds> sounds = m_matrix.nextMaterialSounds(a, b, fallback);
    if (!sounds) {
        ++m_unmatched;
        return;
    }
    static_cast<void>(playSound(sounds->sounds[0], volume * sounds->volumes[0], 1.0F, at, owner));
}

void MaterialSoundPlayer::playAnimSound(float volume, std::uint32_t event, SoundVec at, std::uint32_t owner) {
    const std::optional<MatrixSounds> sounds = m_matrix.nextAnimSounds(event);
    if (!sounds) {
        ++m_unmatched;
        return;
    }
    static_cast<void>(playSound(sounds->sounds[0], volume * sounds->volumes[0], 1.0F, at, owner));
}

MaterialSoundPlayer::FootMaterial MaterialSoundPlayer::remapFootMaterial(std::uint32_t ground) {
    switch (ground) {
    case kTrainRail:
        return {.material = kTrainRailFoot, .volume = 1.0F};
    case kAsphalt:
        return {.material = kConcrete, .volume = 1.0F};
    case kHalfVolumeGround:
        return {.material = kHalfVolumeFoot, .volume = 0.5F};
    default:
        return {.material = ground, .volume = 1.0F};
    }
}

} // namespace coney::audio
