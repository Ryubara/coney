// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/sound_engine.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

#include "core/name_hash.h"

namespace coney::audio {

namespace {

// Admission limits (docs/research/sound.md#play).
constexpr std::size_t kBusyTasks = 100;
constexpr std::size_t kFullTasks = 240;
constexpr std::uint8_t kLowPriority = 20;
constexpr std::uint8_t kLowestPriority = 21;
constexpr int kLowestPerUpdate = 5;
// A positional one-shot farther than its class's far distance plus this plays virtually.
constexpr float kCullMargin = 10.0F;
// A virtual sound of priority 20 or more lasts at most this long.
constexpr double kVirtualLowMs = 500.0;
// Stream sounds below this priority number may take a victim's channel.
constexpr std::uint8_t kStreamStealPriority = 12;
// Victims: priority 5 or more, 4 or more for the last task checked; between equals of 8 or more, the quietest.
constexpr std::uint8_t kVictimPriority = 5;
constexpr std::uint8_t kLastVictimPriority = 4;
constexpr std::uint8_t kQuietTiePriority = 8;
// The priority-21 random volume factor: 1 +- up to 19 %.
constexpr int kLowestVolumeSpread = 19;
// The ears sit half a metre either side of a listener; the far ear's gain (docs/research/sound.md#three-d).
constexpr float kEarOffset = 0.5F;
constexpr float kFarEarWeight = 0.75F;
// The one sound that ignores the options' sound volume.
constexpr std::uint32_t kFullVolumeSound = 0x510bb577;
// The ambient bed's fades.
constexpr float kAmbientFadeMs = 2000.0F;
// The load screens: banks load_00 to load_06.
constexpr int kLoadScreens = 7;
// How far ahead of its voice a sound stream is decoded: half a second of frames.
constexpr int kStreamAheadDivisor = 2;
// Stereo sound effects take two adjacent stream channels of 5-9 (Coney's pairing: the game's pair claim at
// 0x0011b450 is not traced).
constexpr std::array<int, 2> kStereoPairs{5, 7};

// Distance between two points.
float distance(SoundVec a, SoundVec b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return std::sqrt((dx * dx) + (dy * dy) + (dz * dz));
}

// A default random source for an engine given none: xorshift32 from a fixed seed, deterministic.
SoundEngine::RandomRange defaultRandom() {
    return [state = std::uint32_t{0x2545f491}](std::int32_t low, std::int32_t high) mutable {
        state ^= state << 13U;
        state ^= state >> 17U;
        state ^= state << 5U;
        if (high <= low) {
            return low;
        }
        return low + static_cast<std::int32_t>(state % static_cast<std::uint32_t>(high - low + 1));
    };
}

} // namespace

SoundEngine::SoundEngine(Mixer& mixer, SoundTables tables, std::unique_ptr<SoundFiles> files, BankLoader loadBank,
                         RandomRange random)
    : m_mixer(mixer), m_tables(std::move(tables)), m_files(std::move(files)), m_loadBank(std::move(loadBank)),
      m_random(random ? std::move(random) : defaultRandom()), m_tasks(kMaxTasks), m_music(mixer, m_tables) {
    // The first load screen is a random one (0x0010f768).
    m_loadScreenNumber = m_random(0, kLoadScreens - 1);
}

SoundEngine::~SoundEngine() { stopAll(); }

// ---- Tasks ----

SoundEngine::Task* SoundEngine::find(SoundHandle sound) {
    if (!sound.valid()) {
        return nullptr;
    }
    const auto it = std::ranges::find_if(m_tasks, [sound](const Task& t) { return t.live && t.id == sound.id; });
    return it == m_tasks.end() ? nullptr : &*it;
}

const SoundEngine::Task* SoundEngine::find(SoundHandle sound) const {
    if (!sound.valid()) {
        return nullptr;
    }
    const auto it = std::ranges::find_if(m_tasks, [sound](const Task& t) { return t.live && t.id == sound.id; });
    return it == m_tasks.end() ? nullptr : &*it;
}

// Admission (docs/research/sound.md#play, step 2): a busy manager refuses the least important sounds first.
bool SoundEngine::admits(std::uint8_t priority) const {
    const auto alive = static_cast<std::size_t>(std::ranges::count_if(m_tasks, &Task::live));
    if (alive > kFullTasks || (alive > kBusyTasks && priority >= kLowPriority)) {
        return false;
    }
    return !(priority >= kLowestPriority && m_startedThisUpdate >= kLowestPerUpdate);
}

// Whether no live task holds game voice `voice`.
bool SoundEngine::voiceFree(int voice) const {
    return std::ranges::none_of(m_tasks, [voice](const Task& t) {
        return t.live && !t.virtualPlay && (t.voice == voice || t.voice2 == voice);
    });
}

// A factor in 1 +- percent / 100, drawn from the random source (Coney's mapping of the draw onto the range: the page
// gives the range, not the draw).
float SoundEngine::randomFactor(int percent) {
    if (percent <= 0) {
        return 1.0F;
    }
    return 1.0F + (static_cast<float>(m_random(-percent, percent)) / 100.0F);
}

// The game's length of a sound: AudioDevice_Duration, whole seconds only (docs/research/formats/audio.md).
// @orig 0x0014d2d8 AudioDevice_Duration (msaudiodevice.cpp)
double SoundEngine::durationMs(const Task& task) const {
    const int rate = task.record->sampleRate();
    if (rate <= 0) {
        return 0.0;
    }
    const auto bytes = static_cast<std::uint64_t>(std::floor(static_cast<double>(task.record->size) * 3.5));
    const std::uint64_t seconds = bytes / (2 * static_cast<std::uint64_t>(rate)); // whole seconds, rounded down
    return static_cast<double>(seconds) * 1000.0;
}

// Task_FindVictim (docs/research/sound.md#voice-stealing): a mono, real, live task that streams as the new sound does
// with the same small-loop flag and a priority of 5 or more (4 for the last one checked); the least important, and
// between equals of priority 8 or more the quietest. Taken only when less important than the new sound.
// @orig 0x00112700 Task_FindVictim (unknown)
SoundEngine::Task* SoundEngine::findVictim(const Task& task) {
    std::vector<Task*> list;
    for (Task& t : m_tasks) {
        if (t.live && &t != &task) {
            list.push_back(&t);
        }
    }
    std::ranges::sort(list, {}, &Task::order);
    Task* best = nullptr;
    for (std::size_t i = 0; i < list.size(); ++i) {
        Task& t = *list[i];
        const std::uint8_t floor = i + 1 == list.size() ? kLastVictimPriority : kVictimPriority;
        if (t.virtualPlay || t.prepared || t.state != 1 || t.soundClass->stereo() ||
            t.soundClass->streamed() != task.soundClass->streamed() ||
            t.soundClass->smallLoop() != task.soundClass->smallLoop() || t.soundClass->priority < floor) {
            continue;
        }
        const bool lessImportant = best == nullptr || t.soundClass->priority > best->soundClass->priority;
        const bool quieter = best != nullptr && t.soundClass->priority == best->soundClass->priority &&
                             t.soundClass->priority >= kQuietTiePriority &&
                             (t.sent[0] + t.sent[1]) < (best->sent[0] + best->sent[1]);
        if (lessImportant || quieter) {
            best = &t;
        }
    }
    if (best == nullptr || best->soundClass->priority <= task.soundClass->priority) {
        return nullptr;
    }
    return best;
}

// Task_GetVoice (docs/research/sound.md#play, step 5): a stream channel or pair for a streamed sound, an SPU2 voice for
// a bank sample, or a victim's; false when there is none (the sound then plays virtually).
// @orig 0x00112560 Task_GetVoice (unknown)
bool SoundEngine::takeVoice(Task& task) {
    const SoundClass& cls = *task.soundClass;
    if (cls.streamed() && cls.stereo()) {
        for (const int first : kStereoPairs) {
            if (voiceFree(first) && voiceFree(first + 1)) {
                task.voice = first;
                task.voice2 = first + 1;
                return true;
            }
        }
        return false;
    }
    const int low = cls.streamed() ? (cls.smallLoop() ? kFirstSmallLoop : kFirstStream) : kFirstSample;
    const int high = cls.streamed() ? (cls.smallLoop() ? kLastSmallLoop : kLastStream) : kLastSample;
    for (int voice = low; voice <= high; ++voice) {
        if (voiceFree(voice)) {
            task.voice = voice;
            return true;
        }
    }
    if (cls.streamed() && cls.priority >= kStreamStealPriority) {
        return false;
    }
    Task* victim = findVictim(task);
    if (victim == nullptr) {
        return false;
    }
    const int voice = victim->voice;
    victim->state = 3;
    endTask(*victim);
    ++m_stats.stolen;
    task.voice = voice;
    return true;
}

SoundHandle SoundEngine::play(std::uint32_t hash, const SoundPlay& how) { return startTask(hash, how, false); }

// AudioManager_NewTask and AudioManager_Play (docs/research/sound.md#play); a prepared task gets its voice but does not
// start it (a scene soundtrack).
// @orig 0x001120c8 AudioManager_NewTask (unknown)
SoundHandle SoundEngine::startTask(std::uint32_t hash, const SoundPlay& how, bool prepared) {
    // 1. The record by hash.
    const SoundRecord* record = m_tables.find(hash);
    const SoundClass* cls = record != nullptr ? m_tables.classOf(*record) : nullptr;
    if (cls == nullptr) {
        ++m_stats.unknown;
        return {};
    }
    // 2. Admission; 3. a free task.
    const auto free = std::ranges::find(m_tasks, false, &Task::live);
    if (!admits(cls->priority) || free == m_tasks.end()) {
        ++m_stats.refused;
        return {};
    }
    Task& task = *free;
    task = Task{};
    task.live = true;
    task.id = m_nextId;
    m_nextId = m_nextId == UINT32_MAX ? 1 : m_nextId + 1;
    task.order = m_nextOrder++;
    task.record = record;
    task.soundClass = cls;
    task.how = how;
    task.prepared = prepared;
    task.startMs = m_now;
    if (cls->priority >= kLowestPriority) {
        ++m_startedThisUpdate;
        task.randomVolume = randomFactor(kLowestVolumeSpread);
    }
    task.variation = randomFactor(record->pitchVariation);
    if (how.fadeInMs > 0.0F) {
        task.fade = 0.0F;
        task.fadeMode = 1;
        task.fadeStart = m_now;
        task.fadeLength = how.fadeInMs;
    }
    // 4. The 3D cull, and the load screen's virtual plays; 5. a voice.
    const bool positional = cls->positional() && how.position.has_value();
    if (positional && !m_listeners.empty()) {
        task.distance = distance(how.position.value_or(SoundVec{}), m_listeners.front().position);
    }
    const bool culled = positional && !cls->loops() && task.distance > static_cast<float>(cls->far) + kCullMargin;
    const bool loading = m_loadScreen && (positional || cls->stereo());
    task.virtualPlay = culled || loading || !takeVoice(task);
    // 6. Its length; a virtual unimportant sound is short.
    task.lengthMs = durationMs(task);
    if (task.virtualPlay && cls->priority >= kLowPriority) {
        task.lengthMs = std::min(task.lengthMs, kVirtualLowMs);
    }
    if (!how.duckable) {
        m_nonDuckablePlaying = true;
    }
    ++m_stats.started;
    updateTask(task, m_listeners);
    if (!task.virtualPlay) {
        startVoice(task);
    }
    return SoundHandle{task.id};
}

// Starts a task's voice (AudioDevice_Start): a bank sample by hash, or a stream of BFW.SND fed as it plays. A sample
// missing from the bank, or a stream that cannot be read, leaves the task virtual.
// @orig 0x0014caf8 AudioDevice_Start (msaudiodevice.cpp)
void SoundEngine::startVoice(Task& task) {
    const SoundClass& cls = *task.soundClass;
    // Coney's buses: voices and speeches on Speech, the rest on Sfx.
    const Bus bus = cls.directional() || (cls.flags & 0x06) == 0x06 ? Bus::Speech : Bus::Sfx;
    const VoiceParams params{
        .bus = bus, .volume = 0.0F, .priority = static_cast<std::uint8_t>(255 - cls.priority), .paused = task.prepared};
    if (!cls.streamed()) {
        std::shared_ptr<const PcmSound> sample = m_bank.find(task.record->hash);
        if (!sample) {
            ++m_stats.missingSamples;
            task.virtualPlay = true;
            return;
        }
        task.mixerVoice = m_mixer.play(std::move(sample), params);
    } else {
        StreamLayout layout =
            StreamLayout::mono(SoundFile::Sounds, task.record->offset, task.record->size, cls.loops());
        if (cls.stereo()) {
            const StereoLayout* stereo = m_tables.stereoLayout(task.record->hash);
            if (stereo == nullptr) {
                task.virtualPlay = true;
                return;
            }
            layout.channels = 2;
            layout.interleave = stereo->interleave;
            layout.blocks = stereo->blocks;
            layout.lastBlock = stereo->lastBlock;
        }
        const int rate = task.record->sampleRate();
        auto feeder = m_files != nullptr && rate > 0
                          ? StreamFeeder::create(layout, rate, static_cast<std::uint32_t>(rate / kStreamAheadDivisor))
                          : std::unexpected(Error{ErrorCode::NotFound, "no sound files"});
        if (!feeder) {
            task.virtualPlay = true;
            return;
        }
        task.feeder = std::move(*feeder);
        if (auto fed = task.feeder->pump(*m_files); !fed) {
            ++m_stats.readErrors;
        }
        task.mixerVoice = m_mixer.play(task.feeder->stream(), params);
    }
    if (task.sent[0] >= 0.0F) {
        m_mixer.setStereoVolume(task.mixerVoice, task.sent[0], task.sent[1]);
    }
    if (task.rateSent > 0.0F) {
        m_mixer.setRate(task.mixerVoice, task.rateSent);
    }
}

// Ends a task: its voice stops and its game voices are free.
void SoundEngine::endTask(Task& task) {
    m_mixer.stop(task.mixerVoice);
    task = Task{};
}

// Whether a task's owner is one of the players.
bool SoundEngine::ownedByPlayer(const Task& task) const {
    return task.how.owner != 0 && std::ranges::find(m_playerOwners, task.how.owner) != m_playerOwners.end();
}

// A positional task's left and right volumes over the listeners (docs/research/sound.md#three-d): distance
// attenuation, the two-ear pan, the record's and the caller's volumes, the options' sound volume, the fade; the
// loudest over the listeners.
void SoundEngine::positionalVolumes(Task& task, std::span<const Listener> listeners,
                                    std::array<float, 2>& volumes) const {
    const SoundClass& cls = *task.soundClass;
    const SoundVec source = task.how.position.value_or(SoundVec{});
    const float common = (static_cast<float>(task.record->volume) / 100.0F) * task.how.volume * task.randomVolume *
                         m_soundVolume * task.fade * task.how.volumeFactor;
    volumes = {0.0F, 0.0F};
    for (const Listener& listener : listeners) {
        const float d = distance(source, listener.position);
        const auto near = static_cast<float>(cls.near);
        const auto far = static_cast<float>(cls.far);
        float a = 0.0F;
        if (d <= near) {
            a = 1.0F;
        } else if (d < far && far > near) {
            const float t = 1.0F - ((d - near) / (far - near));
            a = t * t;
        }
        // The directional factor's table (0x0050a910) is not listed yet: Coney's stand-in is 1 (an open item on the
        // page), so directional sounds are as loud from behind as in front.
        // The two ears: the nearer gets 1, the farther 0.75 a + 1 - |dL - dR|.
        const float length = std::sqrt((listener.right.x * listener.right.x) + (listener.right.y * listener.right.y) +
                                       (listener.right.z * listener.right.z));
        const SoundVec axis =
            length > 0.0F ? SoundVec{listener.right.x / length, listener.right.y / length, listener.right.z / length}
                          : SoundVec{1.0F, 0.0F, 0.0F};
        const SoundVec leftEar{listener.position.x - (axis.x * kEarOffset), listener.position.y - (axis.y * kEarOffset),
                               listener.position.z - (axis.z * kEarOffset)};
        const SoundVec rightEar{listener.position.x + (axis.x * kEarOffset),
                                listener.position.y + (axis.y * kEarOffset),
                                listener.position.z + (axis.z * kEarOffset)};
        const float dL = distance(source, leftEar);
        const float dR = distance(source, rightEar);
        const float farEar = std::clamp((kFarEarWeight * a) + 1.0F - std::abs(dL - dR), 0.0F, 1.0F);
        const float left = a * (dL <= dR ? 1.0F : farEar) * common;
        const float right = a * (dR <= dL ? 1.0F : farEar) * common;
        volumes[0] = std::max(volumes[0], left);
        volumes[1] = std::max(volumes[1], right);
    }
    if (!listeners.empty()) {
        task.distance = distance(source, listeners.front().position);
    }
    // A duckable directional sound not of a player's ducks under a non-duckable one.
    if (task.how.duckable && cls.directional() && !ownedByPlayer(task) && m_nonDuckablePlaying) {
        volumes[0] *= m_niDuck;
        volumes[1] *= m_niDuck;
    }
}

// Task_Update (docs/research/sound.md#three-d): the fade, the volumes and the rate, each sent only when changed.
// @orig 0x0011a170 Task_Update (unknown)
void SoundEngine::updateTask(Task& task, std::span<const Listener> listeners) {
    // The fade: in, from 0 to 1; out, to 0 and then the sound stops.
    if (task.fadeMode != 0 && task.fadeLength > 0.0) {
        const auto t = static_cast<float>(std::clamp((m_now - task.fadeStart) / task.fadeLength, 0.0, 1.0));
        if (task.fadeMode == 1) {
            task.fade = std::max(task.fade, t);
            if (t >= 1.0F) {
                task.fadeMode = 0;
            }
        } else {
            task.fade = std::min(task.fade, 1.0F - t);
            if (t >= 1.0F) {
                task.state = 2;
            }
        }
    }
    std::array<float, 2> volumes{};
    if (task.soundClass->positional() && task.how.position) {
        positionalVolumes(task, listeners, volumes);
    } else {
        const float options = task.record->hash == kFullVolumeSound ? 1.0F : m_soundVolume;
        const float base = (static_cast<float>(task.record->volume) / 100.0F) * options * task.fade * task.how.volume *
                           task.randomVolume * task.how.volumeFactor;
        volumes = {base * task.how.panLeft, base * task.how.panRight};
    }
    volumes = {std::clamp(volumes[0], 0.0F, 1.0F), std::clamp(volumes[1], 0.0F, 1.0F)};
    if (volumes != task.sent) {
        task.sent = volumes;
        m_mixer.setStereoVolume(task.mixerVoice, volumes[0], volumes[1]);
    }
    // The rate: the record's rate times the random variation, the caller's pitch and the global pitch factor. Pitch
    // updates stop while the sound is paused.
    if (!m_paused) {
        const float rate =
            static_cast<float>(task.record->sampleRate()) * task.variation * task.how.pitch * m_pitchFactor;
        if (rate != task.rateSent) {
            task.rateSent = rate;
            m_mixer.setRate(task.mixerVoice, rate);
        }
    }
}

void SoundEngine::stop(SoundHandle sound, float fadeOutMs) {
    Task* task = find(sound);
    if (task == nullptr) {
        return;
    }
    if (fadeOutMs > 0.0F && !task->virtualPlay) {
        task->fadeMode = 2;
        task->fadeStart = m_now;
        task->fadeLength = fadeOutMs;
        return;
    }
    task->state = 3;
    endTask(*task);
}

void SoundEngine::stopAll() {
    for (Task& task : m_tasks) {
        if (task.live) {
            endTask(task);
        }
    }
}

bool SoundEngine::isPlaying(SoundHandle sound) const { return find(sound) != nullptr; }

bool SoundEngine::isVirtual(SoundHandle sound) const {
    const Task* task = find(sound);
    return task != nullptr && task->virtualPlay;
}

int SoundEngine::voiceOf(SoundHandle sound) const {
    const Task* task = find(sound);
    return task != nullptr && !task->virtualPlay ? task->voice : -1;
}

void SoundEngine::setVolume(SoundHandle sound, float volume) {
    if (Task* task = find(sound)) {
        task->how.volume = volume;
    }
}

void SoundEngine::setPosition(SoundHandle sound, SoundVec position, SoundVec facing) {
    if (Task* task = find(sound)) {
        task->how.position = position;
        task->how.facing = facing;
    }
}

std::optional<std::array<float, 2>> SoundEngine::sentVolumes(SoundHandle sound) const {
    const Task* task = find(sound);
    if (task == nullptr) {
        return std::nullopt;
    }
    return task->sent;
}

// Tasks_Update and the rest of AudioManager_Update (docs/research/sound.md#three-d): every real task updated and fed,
// the finished freed, a virtual task freed once its length has passed (never, if it loops); then the music.
// @orig 0x00112b10 Tasks_Update (unknown)
void SoundEngine::update(float milliseconds, std::span<const Listener> listeners) {
    if (!m_paused) {
        m_now += milliseconds;
    }
    if (!listeners.empty()) {
        m_listeners.assign(listeners.begin(), listeners.end());
    }
    // "A non-duckable sound is playing": set by the live tasks, cleared each update.
    m_nonDuckablePlaying = std::ranges::any_of(m_tasks, [](const Task& t) { return t.live && !t.how.duckable; });
    for (Task& task : m_tasks) {
        if (!task.live) {
            continue;
        }
        if (task.virtualPlay) {
            if (!task.prepared && !task.soundClass->loops() && m_now - task.startMs >= task.lengthMs) {
                endTask(task);
            }
            continue;
        }
        updateTask(task, m_listeners);
        if (task.feeder && m_files != nullptr) {
            if (auto fed = task.feeder->pump(*m_files); !fed) {
                ++m_stats.readErrors;
            }
        }
        const bool voiceEnded = !m_mixer.isPlaying(task.mixerVoice) && (!task.feeder || task.feeder->done());
        if (task.state == 2 || task.state == 3 || voiceEnded) {
            endTask(task);
        }
    }
    m_startedThisUpdate = 0;
    if (m_sceneSound.valid() && !isPlaying(m_sceneSound)) {
        m_sceneSound = {};
        m_music.setScenePlaying(false);
    }
    m_music.update(m_now, m_files.get(), m_random);
}

// ---- Banks ----

std::expected<void, Error> SoundEngine::loadBank(std::string_view name) {
    if (m_deferBankLoads) {
        m_pendingBank = std::string(name);
        return {};
    }
    if (name == m_bank.name()) {
        return {};
    }
    // The samples in sound RAM are overwritten: the tasks playing them stop.
    for (Task& task : m_tasks) {
        if (task.live && !task.soundClass->streamed()) {
            endTask(task);
        }
    }
    m_bank = SoundBank{};
    if (name == "none" || !m_loadBank) {
        return {};
    }
    auto bank = m_loadBank(name, m_tables);
    if (!bank) {
        return std::unexpected(std::move(bank.error()));
    }
    m_bank = std::move(*bank);
    return {};
}

void SoundEngine::startLoadScreen(bool armies) {
    const std::string bank = armies ? std::string("armload") : std::format("load_{:02}", m_loadScreenNumber);
    if (!armies) {
        m_loadScreenNumber = (m_loadScreenNumber + 1) % kLoadScreens;
    }
    const bool defer = std::exchange(m_deferBankLoads, false);
    if (auto loaded = loadBank(bank); !loaded) {
        ++m_stats.bankErrors;
    }
    m_deferBankLoads = defer;
    // The two halves, hard left and hard right.
    m_loadScreenSounds[0] =
        play(crc32(std::format("vags/load_screen/{}_l", bank)), SoundPlay{.panLeft = 1.0F, .panRight = 0.0F});
    m_loadScreenSounds[1] =
        play(crc32(std::format("vags/load_screen/{}_r", bank)), SoundPlay{.panLeft = 0.0F, .panRight = 1.0F});
    m_loadScreen = true;
}

void SoundEngine::endLoadScreen() {
    for (SoundHandle& sound : m_loadScreenSounds) {
        stop(sound);
        sound = {};
    }
    m_loadScreen = false;
    const std::string bank = m_pendingBank != "none" ? m_pendingBank : std::string("sound");
    m_pendingBank = "none";
    const bool defer = std::exchange(m_deferBankLoads, false);
    if (auto loaded = loadBank(bank); !loaded) {
        ++m_stats.bankErrors;
    }
    m_deferBankLoads = defer;
}

// ---- Ambience and interface sounds ----

void SoundEngine::playAmbientTrack(std::uint32_t hash) {
    if (hash == m_ambientHash && isPlaying(m_ambient)) {
        return;
    }
    stop(m_ambient, kAmbientFadeMs);
    m_ambientHash = hash;
    m_ambient = play(hash, SoundPlay{.volume = m_ambientVolume, .fadeInMs = kAmbientFadeMs});
}

void SoundEngine::stopAmbientTrack() {
    stop(m_ambient, kAmbientFadeMs);
    m_ambient = {};
    m_ambientHash = 0;
}

void SoundEngine::setAmbientTrackVolume(float volume) {
    m_ambientVolume = volume;
    setVolume(m_ambient, volume);
}

void SoundEngine::setInterfaceSound(std::size_t index, std::uint32_t hash) {
    if (index >= m_interfaceSounds.size()) {
        m_interfaceSounds.resize(index + 1, 0);
    }
    m_interfaceSounds[index] = hash;
}

SoundHandle SoundEngine::playInterfaceSound(std::size_t index) {
    if (index >= m_interfaceSounds.size() || m_interfaceSounds[index] == 0) {
        return {};
    }
    return play(m_interfaceSounds[index]);
}

// ---- Scene soundtracks ----

SoundHandle SoundEngine::preloadSceneSound(std::uint32_t hash) {
    stopSceneSound();
    m_sceneSound = startTask(hash, SoundPlay{}, true);
    return m_sceneSound;
}

void SoundEngine::startSceneSound() {
    Task* task = find(m_sceneSound);
    if (task == nullptr) {
        return;
    }
    task->prepared = false;
    task->startMs = m_now;
    m_mixer.setPaused(task->mixerVoice, false);
    m_music.setScenePlaying(true);
}

void SoundEngine::stopSceneSound() {
    stop(m_sceneSound);
    m_sceneSound = {};
    m_music.setScenePlaying(false);
}

// ---- Settings ----

void SoundEngine::pause() {
    if (!m_paused) {
        m_paused = true;
        m_mixer.pauseAll();
    }
}

void SoundEngine::resume() {
    if (m_paused) {
        m_paused = false;
        m_mixer.resumeAll();
    }
}

SoundEngineStats SoundEngine::stats() const {
    SoundEngineStats stats = m_stats;
    stats.tasks = static_cast<std::size_t>(std::ranges::count_if(m_tasks, &Task::live));
    stats.virtualTasks =
        static_cast<std::size_t>(std::ranges::count_if(m_tasks, [](const Task& t) { return t.live && t.virtualPlay; }));
    return stats;
}

} // namespace coney::audio
