// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/speech.h"

#include <algorithm>
#include <utility>

namespace coney::audio {

SoundHandle Speech::say(SoundEngine& engine, double human, std::uint32_t hash, const SpeakerPlace& at, bool interrupt,
                        std::string callback, std::optional<double> arg, float volume, bool duckable) {
    if (speaking(engine, human)) {
        if (!interrupt) {
            return {};
        }
        shutUp(engine, human);
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
    m_lines.push_back(Line{.human = human, .sound = sound, .callback = std::move(callback), .arg = arg});
    ++m_said;
    return sound;
}

bool Speech::speaking(const SoundEngine& engine, double human) const {
    return std::ranges::any_of(
        m_lines, [&engine, human](const Line& line) { return line.human == human && engine.isPlaying(line.sound); });
}

void Speech::shutUp(SoundEngine& engine, double human) {
    std::erase_if(m_lines, [&engine, human](const Line& line) {
        if (line.human != human) {
            return false;
        }
        engine.stop(line.sound);
        return true;
    });
}

std::vector<Speech::Ended> Speech::update(SoundEngine& engine, const SpeakerLocator& locate) {
    std::vector<Ended> ended;
    std::erase_if(m_lines, [&](Line& line) {
        if (!engine.isPlaying(line.sound)) {
            ended.push_back(Ended{.callback = std::move(line.callback), .arg = line.arg});
            return true;
        }
        // Coney's stand-in: the line follows its speaker.
        if (locate) {
            if (const std::optional<SpeakerPlace> at = locate(line.human); at) {
                engine.setPosition(line.sound, at->position, at->facing);
            }
        }
        return false;
    });
    return ended;
}

void Speech::clear(SoundEngine* engine) {
    if (engine != nullptr) {
        for (const Line& line : m_lines) {
            engine->stop(line.sound);
        }
    }
    m_lines.clear();
}

} // namespace coney::audio
