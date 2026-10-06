// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/ambient_emitters.h"

#include <algorithm>
#include <cstddef>

namespace coney::audio {

namespace {

// The draw that places a sound along an emitter's line: a whole number in [0, kLineSteps].
constexpr std::int32_t kLineSteps = 1000;

} // namespace

void AmbientEmitters::setSound(int slot, std::uint32_t hash) {
    if (slot < 0 || slot > kMaxSlot) {
        return;
    }
    const auto index = static_cast<std::size_t>(slot);
    if (index >= m_table.size()) {
        m_table.resize(index + 1, 0);
    }
    m_table.at(index) = hash;
}

std::uint32_t AmbientEmitters::sound(int slot) const {
    if (slot < 0 || static_cast<std::size_t>(slot) >= m_table.size()) {
        return 0;
    }
    return m_table.at(static_cast<std::size_t>(slot));
}

int AmbientEmitters::add(const AmbientEmitterSetup& setup) {
    // A name already used refers to the existing emitter: it takes the new setup and starts its cycle again.
    const auto same =
        std::ranges::find_if(m_emitters, [&setup](const Emitter& e) { return e.setup.name == setup.name; });
    if (same != m_emitters.end() && !setup.name.empty()) {
        same->setup = setup;
        same->waiting = false;
        return static_cast<int>(same - m_emitters.begin());
    }
    m_emitters.push_back(Emitter{.setup = setup});
    return static_cast<int>(m_emitters.size() - 1);
}

bool AmbientEmitters::setPositions(std::string_view name, std::span<const SoundVec> positions) {
    const auto found = std::ranges::find_if(m_emitters, [name](const Emitter& e) { return e.setup.name == name; });
    if (found == m_emitters.end()) {
        return false;
    }
    const std::size_t kept = std::min(positions.size(), kMaxPositions);
    found->positions.assign(positions.begin(), positions.begin() + static_cast<std::ptrdiff_t>(kept));
    return true;
}

void AmbientEmitters::update(SoundEngine& engine) {
    const double now = engine.now();
    for (Emitter& emitter : m_emitters) {
        // Coney's stand-in: one sound at a time; a looping one plays on.
        if (emitter.playing.valid() && engine.isPlaying(emitter.playing)) {
            continue;
        }
        emitter.playing = {};
        if (!emitter.waiting) {
            // Coney's stand-in: a pause of a random whole number of seconds in [minDelay, maxDelay].
            const auto low = static_cast<std::int32_t>(std::min(emitter.setup.minDelay, emitter.setup.maxDelay));
            const auto high = static_cast<std::int32_t>(std::max(emitter.setup.minDelay, emitter.setup.maxDelay));
            emitter.nextMs = now + (static_cast<double>(engine.random(low, high)) * 1000.0);
            emitter.waiting = true;
        }
        if (now < emitter.nextMs) {
            continue;
        }
        emitter.waiting = false;
        const std::uint32_t hash = pickSound(emitter, engine);
        if (hash == 0) {
            continue;
        }
        emitter.playing = engine.play(hash, SoundPlay{.position = pickPosition(emitter, engine)});
        ++m_plays;
    }
}

void AmbientEmitters::clearEmitters(SoundEngine* engine) {
    if (engine != nullptr) {
        for (const Emitter& emitter : m_emitters) {
            engine->stop(emitter.playing);
        }
    }
    m_emitters.clear();
}

std::uint32_t AmbientEmitters::pickSound(const Emitter& emitter, SoundEngine& engine) const {
    if (emitter.setup.slot < 0) {
        return emitter.setup.sound;
    }
    // A random one of its `count` slots (one slot for a count of 0 or 1).
    const auto last = static_cast<std::int32_t>(std::max<std::uint32_t>(emitter.setup.count, 1) - 1);
    return sound(emitter.setup.slot + engine.random(0, last));
}

SoundVec AmbientEmitters::pickPosition(const Emitter& emitter, SoundEngine& engine) {
    // Coney's stand-in: a random one of its positions, else a random point of its line.
    if (!emitter.positions.empty()) {
        const auto last = static_cast<std::int32_t>(emitter.positions.size() - 1);
        return emitter.positions.at(static_cast<std::size_t>(engine.random(0, last)));
    }
    const float t = static_cast<float>(engine.random(0, kLineSteps)) / static_cast<float>(kLineSteps);
    const SoundVec& a = emitter.setup.from;
    const SoundVec& b = emitter.setup.to;
    return SoundVec{a.x + ((b.x - a.x) * t), a.y + ((b.y - a.y) * t), a.z + ((b.z - a.z) * t)};
}

} // namespace coney::audio
