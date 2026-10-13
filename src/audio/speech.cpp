// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/speech.h"

#include <algorithm>
#include <utility>

namespace coney::audio {

Speech::Voice* Speech::find(double human) {
    const auto it = std::ranges::find(m_voices, human, &Voice::human);
    return it == m_voices.end() ? nullptr : &*it;
}

const Speech::Voice* Speech::find(double human) const {
    const auto it = std::ranges::find(m_voices, human, &Voice::human);
    return it == m_voices.end() ? nullptr : &*it;
}

bool Speech::alive(const SoundEngine& engine, const Voice& voice) {
    return voice.sound.valid() && engine.isPlaying(voice.sound);
}

void Speech::stop(SoundEngine& engine, Voice& voice) {
    engine.stop(voice.sound);
    voice.sound = {};
    // The callback stays: the next update runs it (docs/research/sound.md#speech-cut-in).
    voice.mayCut = true;
}

SoundHandle Speech::say(SoundEngine& engine, double human, std::uint32_t hash, const SpeakerPlace& at, Start start,
                        std::string callback, std::optional<double> arg, float volume, bool duckable) {
    if (Voice* voice = find(human); voice != nullptr) {
        const bool playing = alive(engine, *voice);
        if (start == Start::Waiting) {
            // Not over a live line, nor in the update after a line that may not be cut ended (its flag is still clear).
            if (playing || !voice->mayCut) {
                return {};
            }
        } else if (playing) {
            // Over a line that may not be cut: nothing. Otherwise the line stops first (unforced, so it does).
            if (!voice->mayCut) {
                return {};
            }
            stop(engine, *voice);
        }
    }
    // The owner is the speaker, so his lines are a player's when he is one (SoundEngine::setPlayerOwners()).
    SoundPlay how;
    how.volume = volume;
    how.duckable = duckable;
    how.owner = static_cast<std::uint32_t>(human);
    how.position = at.position;
    how.facing = at.facing;
    const SoundHandle sound = engine.play(hash, how);
    if (!sound.valid()) {
        return {};
    }
    Voice* voice = find(human);
    if (voice == nullptr) {
        voice = &m_voices.emplace_back();
        voice->human = human;
    }
    // A line that plays writes its own callback over the human's, whether it has one or not.
    voice->sound = sound;
    voice->callback = std::move(callback);
    voice->arg = arg;
    voice->mayCut = start == Start::Waiting;
    ++m_said;
    return sound;
}

bool Speech::speaking(const SoundEngine& engine, double human) const {
    const Voice* voice = find(human);
    return voice != nullptr && alive(engine, *voice);
}

void Speech::shutUp(SoundEngine& engine, double human, bool force) {
    Voice* voice = find(human);
    if (voice == nullptr || !alive(engine, *voice) || (!force && !voice->mayCut)) {
        return;
    }
    stop(engine, *voice);
}

std::vector<Speech::Ended> Speech::update(SoundEngine& engine, const SpeakerLocator& locate) {
    std::vector<Ended> ended;
    for (Voice& voice : m_voices) {
        if (alive(engine, voice)) {
            // Coney's stand-in: the line follows its speaker.
            if (locate) {
                if (const std::optional<SpeakerPlace> at = locate(voice.human); at) {
                    engine.setPosition(voice.sound, at->position, at->facing);
                }
            }
            continue;
        }
        voice.sound = {};
        if (!voice.callback.empty()) {
            // The line is gone: its callback runs, once. The flag is set an update later, by the branch below.
            ended.push_back(Ended{.callback = std::move(voice.callback), .arg = voice.arg});
            voice.callback.clear();
            voice.arg.reset();
        } else {
            voice.mayCut = true;
        }
    }
    // A human with no line, no callback and a flag that is set is the default: forget him.
    std::erase_if(m_voices,
                  [](const Voice& voice) { return !voice.sound.valid() && voice.callback.empty() && voice.mayCut; });
    return ended;
}

void Speech::clear(SoundEngine* engine) {
    if (engine != nullptr) {
        for (const Voice& voice : m_voices) {
            if (voice.sound.valid()) {
                engine->stop(voice.sound);
            }
        }
    }
    m_voices.clear();
}

std::size_t Speech::lines() const {
    return static_cast<std::size_t>(
        std::ranges::count_if(m_voices, [](const Voice& voice) { return voice.sound.valid(); }));
}

} // namespace coney::audio
