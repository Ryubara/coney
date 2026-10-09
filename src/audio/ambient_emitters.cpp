// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/ambient_emitters.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <limits>

namespace coney::audio {

namespace {

// The longest name an emitter keeps (`+0xad`).
constexpr std::size_t kNameChars = 31;
// The range a -1 range adds to the first sound's far distance, m.
constexpr float kDefaultRangeExtra = 10.0F;
// The ambient factor while a scene plays (Ambient_GetVolume).
constexpr float kSceneFactor = 0.75F;
// The duck of a `music` emitter (Ambient_GetDuck).
constexpr float kMusicDuck = 0.5F;
// The modes (docs/research/sound.md#ambient).
constexpr std::uint8_t kModeTimed = 2;
constexpr std::uint8_t kModeLoop = 3;
constexpr std::uint8_t kModeFirstAtOnce = 4;
constexpr std::uint8_t kModeShort = 5;
// The filters.
constexpr std::uint8_t kFilterUncovered = 0;
constexpr std::uint8_t kFilterCovered = 1;
// The plays counter's value that switches the emitter off.
constexpr int kPlaysOff = -1;

// Takes one play off a limited count (a negative count is unlimited); returns whether that ran the count out.
bool spendPlay(int& plays) {
    if (plays < 0) {
        return false;
    }
    --plays;
    return plays == kPlaysOff;
}

// The distance from `at` to the nearest listener (infinite with none).
float nearestDistance(SoundVec at, std::span<const SoundVec> listeners) {
    float best = std::numeric_limits<float>::infinity();
    for (const SoundVec& l : listeners) {
        best = std::min(best, std::hypot(l.x - at.x, l.y - at.y, l.z - at.z));
    }
    return best;
}

// The far distance of the sound `hash` (0 when it is not in the sound list).
float farOf(const SoundEngine& engine, std::uint32_t hash) {
    const SoundRecord* record = engine.tables().find(hash);
    const SoundClass* soundClass = record != nullptr ? engine.tables().classOf(*record) : nullptr;
    return soundClass != nullptr ? static_cast<float>(soundClass->far) : 0.0F;
}

// The signed byte the plays counter is stored as.
int playsByte(int plays) { return static_cast<int>(static_cast<std::int8_t>(static_cast<std::uint8_t>(plays))); }

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

AmbientEmitters::Kind AmbientEmitters::kindOf(std::string_view name) {
    // `_DAM_` and `_FHT_` as written; `music` anywhere in the lower-case name (the last test wins).
    Kind kind = Kind::Plain;
    if (name.find("_DAM_") != std::string_view::npos) {
        kind = Kind::Damage;
    }
    if (name.find("_FHT_") != std::string_view::npos) {
        kind = Kind::Fight;
    }
    std::string lower(name);
    std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower.find("music") != std::string::npos) {
        kind = Kind::Music;
    }
    return kind;
}

int AmbientEmitters::add(const AmbientEmitterSetup& setup, SoundEngine& engine) {
    const std::string name = setup.name.substr(0, kNameChars);
    // The same name, sound and first point: that emitter, switched on.
    const auto same = std::ranges::find_if(m_emitters, [&](const Emitter& e) {
        return e.setup.name == name && e.setup.sound == setup.sound && e.setup.from.x == setup.from.x &&
               e.setup.from.y == setup.from.y && e.setup.from.z == setup.from.z;
    });
    if (same != m_emitters.end()) {
        same->enabled = true;
        return static_cast<int>(same - m_emitters.begin());
    }
    if (m_emitters.size() >= kMaxEmitters) {
        return -1;
    }
    Emitter e{.setup = setup, .points = {}};
    e.setup.name = name;
    e.points = {setup.to};
    e.plays = playsByte(setup.plays);
    e.mode = setup.mode;
    e.setup.filter = setup.filter > 2 ? 0 : setup.filter;
    e.kind = kindOf(name);
    e.lastMs = engine.now();
    e.delay = engine.random(static_cast<std::int32_t>(setup.minDelay), static_cast<std::int32_t>(setup.maxDelay));
    // The first slot: a random one of its count; the range's sound is the first slot's.
    std::uint32_t first = setup.sound;
    if (setup.slot != -1) {
        const auto count = static_cast<std::int32_t>(setup.count);
        e.nextSlot = setup.slot + (count > 0 ? engine.random(0, count - 1) : 0);
        first = sound(setup.slot);
    }
    e.range = setup.range == -1.0F ? farOf(engine, first) + kDefaultRangeExtra : setup.range;
    m_emitters.push_back(std::move(e));
    return static_cast<int>(m_emitters.size() - 1);
}

bool AmbientEmitters::setPositions(std::string_view name, std::span<const SoundVec> positions) {
    const auto found = std::ranges::find_if(m_emitters, [name](const Emitter& e) { return e.setup.name == name; });
    if (found == m_emitters.end() || positions.empty()) {
        return false;
    }
    const std::size_t kept = std::min(positions.size(), kMaxPositions);
    found->points.assign(positions.begin(), positions.begin() + static_cast<std::ptrdiff_t>(kept));
    return true;
}

void AmbientEmitters::setEnabled(int id, bool on) {
    if (id >= 0 && static_cast<std::size_t>(id) < m_emitters.size()) {
        m_emitters.at(static_cast<std::size_t>(id)).enabled = on;
    }
}

void AmbientEmitters::setVolume(int id, float volume, SoundEngine& engine) {
    if (id < 0 || static_cast<std::size_t>(id) >= m_emitters.size()) {
        return;
    }
    Emitter& e = m_emitters.at(static_cast<std::size_t>(id));
    e.volume = volume;
    engine.setVolume(e.playing, volume);
}

std::uint32_t AmbientEmitters::nextSound(Emitter& emitter) const {
    if (emitter.setup.slot == -1) {
        return emitter.setup.sound;
    }
    const std::uint32_t hash = sound(emitter.nextSlot);
    const int last = emitter.setup.slot + static_cast<int>(emitter.setup.count) - 1;
    emitter.nextSlot = emitter.nextSlot + 1 > last ? emitter.setup.slot : emitter.nextSlot + 1;
    return hash;
}

void AmbientEmitters::play(Emitter& emitter, SoundEngine& engine, SoundVec at, float factor) {
    const std::uint32_t hash = nextSound(emitter);
    emitter.playing = engine.play(hash, SoundPlay{.volume = emitter.volume, .volumeFactor = factor, .position = at});
    emitter.playingHash = hash;
    if (emitter.playing.valid()) {
        ++m_plays;
    }
}

void AmbientEmitters::playTimed(Emitter& emitter, SoundEngine& engine, float factor, int& started) {
    const auto last = static_cast<std::int32_t>(emitter.points.size()) - 1;
    const SoundVec at = emitter.points.at(static_cast<std::size_t>(last > 0 ? engine.random(0, last) : 0));
    play(emitter, engine, at, factor);
    ++started;
}

void AmbientEmitters::stop(Emitter& emitter, SoundEngine& engine) {
    if (emitter.playing.valid()) {
        engine.stop(emitter.playing);
        emitter.playing = {};
    }
}

void AmbientEmitters::redraw(Emitter& emitter, SoundEngine& engine, double now) {
    emitter.lastMs = now;
    emitter.delay = engine.random(static_cast<std::int32_t>(emitter.setup.minDelay),
                                  static_cast<std::int32_t>(emitter.setup.maxDelay));
}

bool AmbientEmitters::kindAllows(const Emitter& emitter, const AmbientWorld& world) {
    switch (emitter.kind) {
    case Kind::Damage:
        return world.damWindow && !world.fightTimer;
    case Kind::Fight:
        return world.fightTimer;
    default:
        return true;
    }
}

void AmbientEmitters::outOfRange(Emitter& emitter, SoundEngine& engine, float distance) {
    if (emitter.playing.valid() && distance > farOf(engine, emitter.playingHash)) {
        stop(emitter, engine);
    }
}

bool AmbientEmitters::heard(const Emitter& emitter, const AmbientWorld& world) {
    // Filter 0 skips a player on covered ground, 1 one off it, 2 none.
    return std::ranges::any_of(world.playersCovered, [&emitter](bool covered) {
        return !((emitter.setup.filter == kFilterUncovered && covered) ||
                 (emitter.setup.filter == kFilterCovered && !covered));
    });
}

void AmbientEmitters::update(SoundEngine& engine, const AmbientWorld& world) {
    const double now = engine.now();
    if (m_lastUpdateMs + kUpdateMs >= now) {
        return;
    }
    m_lastUpdateMs = now;
    int started = 0;
    const float factor = world.scenePlaying ? kSceneFactor : 1.0F;
    // With no player nothing runs.
    if (world.playersCovered.empty()) {
        return;
    }
    for (Emitter& e : m_emitters) {
        if (e.playing.valid() && !engine.isPlaying(e.playing)) {
            e.playing = {};
        }
        // 1. An emitter switched off, or that no player hears, stops its sound.
        if (!e.enabled || !heard(e, world)) {
            stop(e, engine);
            continue;
        }
        // 2. The playing sound takes the ambient factor; a `music` emitter's the duck instead.
        engine.setVolumeFactor(e.playing, factor);
        if (e.kind == Kind::Music && world.musicDuck) {
            engine.setVolumeFactor(e.playing, kMusicDuck);
        }
        // 3. The distance from its first point to the nearest listener, then the mode.
        const float distance = nearestDistance(e.setup.from, world.listeners);
        const bool inRange = distance < e.range;
        const bool playing = e.playing.valid();
        switch (e.mode) {
        case 1:
        case kModeTimed:
            if (!inRange) {
                outOfRange(e, engine, distance);
            } else if (started < kMaxNewPerUpdate && kindAllows(e, world) &&
                       now > e.lastMs + (static_cast<double>(e.delay) * 1000.0)) {
                // Each play takes one off a limited count; the count running out switches it off.
                if (!playing) {
                    if (spendPlay(e.plays)) {
                        e.enabled = false;
                        break;
                    }
                    playTimed(e, engine, factor, started);
                }
                if (e.plays != 0) {
                    redraw(e, engine, now);
                } else {
                    e.lastMs = now;
                    e.delay = 1;
                }
            }
            break;
        case kModeLoop:
            if (inRange) {
                if (!playing) {
                    const float loopFactor =
                        factor == 1.0F && e.kind == Kind::Music && world.musicDuck ? kMusicDuck : factor;
                    play(e, engine, e.points.front(), loopFactor);
                }
            } else {
                stop(e, engine);
            }
            break;
        case kModeFirstAtOnce:
            if (!inRange) {
                outOfRange(e, engine, distance);
            } else if (started < kMaxNewPerUpdate && kindAllows(e, world)) {
                if (spendPlay(e.plays)) {
                    e.enabled = false;
                    break;
                }
                playTimed(e, engine, factor, started);
                e.mode = kModeTimed;
                redraw(e, engine, now);
            }
            break;
        case kModeShort:
            if (!inRange || playing) {
                stop(e, engine);
            } else if (started < kMaxNewPerUpdate && kindAllows(e, world)) {
                if (e.lastMs + (static_cast<double>(e.delay) * 1000.0) <= now) {
                    // The count running out switches it off after this play.
                    if (spendPlay(e.plays)) {
                        e.enabled = false;
                    }
                    playTimed(e, engine, factor, started);
                    redraw(e, engine, now);
                } else {
                    e.lastMs = now;
                    e.delay = 0;
                }
            }
            break;
        default:
            break;
        }
    }
}

void AmbientEmitters::clearEmitters(SoundEngine* engine) {
    if (engine != nullptr) {
        for (Emitter& emitter : m_emitters) {
            stop(emitter, *engine);
        }
    }
    m_emitters.clear();
}

} // namespace coney::audio
