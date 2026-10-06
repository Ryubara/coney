// SPDX-License-Identifier: GPL-3.0-or-later
// Coney's sound output (no @orig): docs/research/sound.md#coneys-implementation.
#include "platform/audio_output.h"

#include <format>
#include <utility>

#include "audio/sound_bank.h"
#include "audio/sound_data.h"
#include "audio/sound_engine.h"
#include "audio/sound_stream.h"
#include "audio/test_tone.h"
#include "fileio/wad.h"

namespace coney::platform {

namespace {

// The name of the master volume, AudioControls' index 0; the buses follow.
constexpr std::string_view kMasterName = "Master";
// The game time of one fixed step, ms.
constexpr float kStepMilliseconds = 1000.0F / 30.0F;

} // namespace

AudioOutput::AudioOutput() : m_mixer(std::make_unique<audio::Mixer>()), m_sounds(*m_mixer) {}

AudioOutput::~AudioOutput() {
    m_device.reset(); // no callback into the mixer after this
}

std::expected<std::unique_ptr<AudioOutput>, Error> AudioOutput::start(AudioSink sink) {
    // The constructor is private, so make_unique cannot reach it.
    std::unique_ptr<AudioOutput> output(new AudioOutput());
    if (sink == AudioSink::Offline) {
        output->m_offline.emplace(*output->m_mixer);
        return output;
    }
    auto device = SdlAudioDevice::open(*output->m_mixer);
    if (!device) {
        return std::unexpected(std::move(device.error()));
    }
    output->m_device = std::move(*device);
    return output;
}

void AudioOutput::endFrame(std::uint32_t steps) {
    m_sounds.update(static_cast<float>(steps) * kStepMilliseconds);
    if (m_offline) {
        m_offline->pullStep();
    }
    m_mixer->collect();
}

std::expected<void, Error> AudioOutput::startEngine(const io::Wad& wad) {
    auto tables = audio::loadSoundTables(wad);
    if (!tables) {
        return std::unexpected(std::move(tables.error()));
    }
    auto files = audio::DiscSoundFiles::open(wad.disc());
    if (!files) {
        return std::unexpected(std::move(files.error()));
    }
    // Banks come from the WAD; the random factors from the engine's own deterministic source (Coney's choice: the
    // game's shared random would shift the scripts' draws).
    auto loadBank = [&wad](std::string_view name, const audio::SoundTables& soundTables) {
        return audio::loadSoundBank(wad, name, soundTables);
    };
    m_sounds.attach(std::make_unique<audio::SoundEngine>(*m_mixer, std::move(*tables), std::move(*files),
                                                         std::move(loadBank), audio::SoundEngine::RandomRange{}));
    return {};
}

std::string AudioOutput::startLine() const {
    return m_device ? std::format("audio: {}, {} Hz stereo, {} voices\n", m_device->name(), audio::kOutputRate,
                                  audio::kVoiceCount)
                    : std::string("audio: offline (test mode), no device\n");
}

std::string AudioOutput::summary() const {
    if (m_offline) {
        return m_offline->summary();
    }
    return std::format("audio: {} frames mixed\n", m_mixer->stats().framesMixed);
}

std::string_view AudioOutput::volumeName(std::size_t index) const {
    return index == 0 ? kMasterName : audio::kBusNames.at(index - 1);
}

float AudioOutput::volume(std::size_t index) const {
    return index == 0 ? m_mixer->masterVolume() : m_mixer->busVolume(static_cast<audio::Bus>(index - 1));
}

void AudioOutput::setVolume(std::size_t index, float volume) {
    if (index == 0) {
        m_mixer->setMasterVolume(volume);
    } else if (index <= audio::kBusCount) {
        m_mixer->setBusVolume(static_cast<audio::Bus>(index - 1), volume);
    }
}

bool AudioOutput::testTone() const { return m_mixer->isPlaying(m_toneVoice); }

void AudioOutput::setTestTone(bool on) {
    if (!on) {
        m_mixer->stop(m_toneVoice);
        m_toneVoice = {};
        return;
    }
    if (testTone()) {
        return;
    }
    if (!m_tone) {
        m_tone = std::make_shared<const audio::PcmSound>(audio::makeToneSweep());
    }
    m_toneVoice = m_mixer->play(m_tone, audio::VoiceParams{.bus = audio::Bus::Sfx});
}

debug::AudioStatus AudioOutput::status() const {
    const audio::MixerStats stats = m_mixer->stats();
    return debug::AudioStatus{.device = m_device ? m_device->name() : std::string("offline (test mode)"),
                              .voicesPlaying = m_mixer->voicesPlaying(),
                              .voiceCount = audio::kVoiceCount,
                              .framesMixed = stats.framesMixed,
                              .sampleRate = audio::kOutputRate,
                              .voicesStolen = stats.voicesStolen,
                              .playsDropped = stats.playsDropped};
}

} // namespace coney::platform
