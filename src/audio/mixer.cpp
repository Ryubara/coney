// SPDX-License-Identifier: GPL-3.0-or-later
// Coney's software mixer (no @orig): the original mixes on the PS2's sound processor, whose use by the game is not
// traced yet (docs/research/sound.md). The volume and pan laws below are Coney's choices until it is.
#include "audio/mixer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

#include "core/assert.h"

namespace coney::audio {

namespace {

// 1.0 in the Q15 gains: a gain of kUnity passes a sample unchanged.
constexpr std::int32_t kUnity = 1 << 15;
// The output's channels, as a size for indexing.
constexpr auto kChannels = static_cast<std::size_t>(kOutputChannels);
// 1.0 in the resampler's Q32 phase.
constexpr std::uint64_t kPhaseOne = std::uint64_t{1} << 32;

// `value` limited to [low, high]; NaN becomes `low`, so a bad float can never reach the integer mix path.
float clampFinite(float value, float low, float high) {
    if (!(value >= low)) { // also true for NaN
        return low;
    }
    return std::min(value, high);
}

// A gain from 0 to 1 as Q15, rounded to nearest.
std::int32_t toQ15(float gain) {
    return static_cast<std::int32_t>(std::lround(static_cast<double>(clampFinite(gain, 0.0F, 1.0F)) * kUnity));
}

} // namespace

Mixer::Mixer() {
    m_busVolume.fill(1.0F);
    m_mixBusVolume.fill(1.0F);
}

Mixer::~Mixer() = default;

// ---- Game thread ----

// Queues a play: the held sound or stream stays alive for the voice, and the command carries a raw pointer to it.
VoiceHandle Mixer::startPlay(Command command, Held held) {
    command.kind = CommandKind::Play;
    command.voice = m_nextId;
    command.sequence = m_nextSequence;
    if (!m_queue.push(command)) {
        ++m_commandsDropped;
        return {};
    }
    ++m_nextSequence;
    held.voice = command.voice;
    held.sequence = command.sequence;
    m_held.push_back(std::move(held));
    // Never 0, which is the empty slot's and the invalid handle's id.
    m_nextId = m_nextId == std::numeric_limits<std::uint32_t>::max() ? 1 : m_nextId + 1;
    return VoiceHandle{command.voice};
}

VoiceHandle Mixer::play(std::shared_ptr<const PcmSound> sound, const VoiceParams& params) {
    CONEY_ASSERT(sound != nullptr);
    Command command;
    command.params = params;
    command.sound = sound.get();
    return startPlay(command, Held{.sound = std::move(sound)});
}

VoiceHandle Mixer::play(std::shared_ptr<PcmStream> stream, const VoiceParams& params) {
    CONEY_ASSERT(stream != nullptr);
    Command command;
    command.params = params;
    command.stream = stream.get();
    return startPlay(command, Held{.stream = std::move(stream)});
}

// Queues `command` with the next sequence number; a full queue loses it and counts the loss.
void Mixer::send(const Command& command) {
    Command numbered = command;
    numbered.sequence = m_nextSequence;
    if (!m_queue.push(numbered)) {
        ++m_commandsDropped;
        return;
    }
    ++m_nextSequence;
}

// Queues a command about one voice; an invalid handle sends nothing.
void Mixer::sendVoice(CommandKind kind, VoiceHandle voice, float value, bool flag) {
    if (!voice.valid()) {
        return;
    }
    send(Command{.kind = kind, .voice = voice.id, .value = value, .flag = flag});
}

void Mixer::stop(VoiceHandle voice) { sendVoice(CommandKind::Stop, voice, 0.0F); }

void Mixer::stopAll() { send(Command{.kind = CommandKind::StopAll}); }

void Mixer::setVolume(VoiceHandle voice, float volume) { sendVoice(CommandKind::Volume, voice, volume); }

void Mixer::setPan(VoiceHandle voice, float pan) { sendVoice(CommandKind::Pan, voice, pan); }

void Mixer::setPitch(VoiceHandle voice, float pitch) { sendVoice(CommandKind::Pitch, voice, pitch); }

void Mixer::setPaused(VoiceHandle voice, bool paused) { sendVoice(CommandKind::Pause, voice, 0.0F, paused); }

void Mixer::setBusVolume(Bus bus, float volume) {
    const float clamped = clampFinite(volume, 0.0F, 1.0F);
    m_busVolume.at(static_cast<std::size_t>(bus)) = clamped;
    send(Command{.kind = CommandKind::BusVolume, .value = clamped, .bus = bus});
}

float Mixer::busVolume(Bus bus) const { return m_busVolume.at(static_cast<std::size_t>(bus)); }

void Mixer::setMasterVolume(float volume) {
    m_masterVolume = clampFinite(volume, 0.0F, 1.0F);
    send(Command{.kind = CommandKind::MasterVolume, .value = m_masterVolume});
}

// Whether a mix slot plays voice `voice` now. Acquire: once a slot no longer shows the voice, the mix thread is done
// with its sound.
bool Mixer::slotHolds(std::uint32_t voice) const {
    return std::ranges::any_of(m_slotIds,
                               [voice](const auto& slot) { return slot.load(std::memory_order_acquire) == voice; });
}

bool Mixer::isPlaying(VoiceHandle voice) const {
    if (!voice.valid()) {
        return false;
    }
    const auto held = std::ranges::find(m_held, voice.id, &Held::voice);
    if (held == m_held.end()) {
        return false;
    }
    // Read the applied count before the slots: a play applied by then has its slot set already.
    if (held->sequence > m_applied.load(std::memory_order_acquire)) {
        return true; // still queued
    }
    return slotHolds(voice.id);
}

std::size_t Mixer::voicesPlaying() const {
    return static_cast<std::size_t>(
        std::ranges::count_if(m_slotIds, [](const auto& slot) { return slot.load(std::memory_order_acquire) != 0; }));
}

void Mixer::collect() {
    const std::uint64_t applied = m_applied.load(std::memory_order_acquire);
    std::erase_if(m_held,
                  [this, applied](const Held& held) { return held.sequence <= applied && !slotHolds(held.voice); });
}

MixerStats Mixer::stats() const {
    return MixerStats{.framesMixed = m_framesMixed.load(std::memory_order_relaxed),
                      .voicesStolen = m_stolen.load(std::memory_order_relaxed),
                      .playsDropped = m_dropped.load(std::memory_order_relaxed),
                      .commandsDropped = m_commandsDropped};
}

// ---- Mix thread ----

// Applies every queued command in order, publishing each one's sequence number after it lands.
void Mixer::applyCommands() {
    while (const std::optional<Command> command = m_queue.pop()) {
        apply(*command);
        m_applied.store(command->sequence, std::memory_order_release);
    }
}

// The slot's voice playing `id`, or null when it has ended (or never started).
Mixer::Voice* Mixer::find(std::uint32_t id) {
    const auto found = std::ranges::find_if(m_voices, [id](const Voice& v) { return v.active && v.id == id; });
    return found == m_voices.end() ? nullptr : &*found;
}

// Carries out one command.
void Mixer::apply(const Command& command) {
    switch (command.kind) {
    case CommandKind::Play:
        startVoice(command);
        return;
    case CommandKind::StopAll:
        for (std::size_t slot = 0; slot < m_voices.size(); ++slot) {
            endVoice(slot);
        }
        return;
    case CommandKind::BusVolume:
        m_mixBusVolume.at(static_cast<std::size_t>(command.bus)) = command.value;
        for (Voice& voice : m_voices) {
            updateGains(voice);
        }
        return;
    case CommandKind::MasterVolume:
        m_mixMasterVolume = command.value;
        for (Voice& voice : m_voices) {
            updateGains(voice);
        }
        return;
    default:
        break;
    }
    // The rest act on one voice, which may have ended since the command was sent.
    Voice* voice = find(command.voice);
    if (voice == nullptr) {
        return;
    }
    switch (command.kind) {
    case CommandKind::Stop:
        endVoice(static_cast<std::size_t>(voice - m_voices.data()));
        break;
    case CommandKind::Volume:
        voice->volume = clampFinite(command.value, 0.0F, 1.0F);
        updateGains(*voice);
        break;
    case CommandKind::Pan:
        voice->pan = clampFinite(command.value, -1.0F, 1.0F);
        updateGains(*voice);
        break;
    case CommandKind::Pitch:
        voice->pitch = clampFinite(command.value, kMinPitch, kMaxPitch);
        updateStep(*voice);
        break;
    case CommandKind::Pause:
        voice->paused = command.flag;
        break;
    default:
        break;
    }
}

// Starts a play in a free slot, or steals the lowest-priority voice (the oldest among equals) when the new play's
// priority is at least as high; otherwise the play is dropped. **Coney's choice** of policy until the game's is traced.
void Mixer::startVoice(const Command& command) {
    auto slot = std::ranges::find(m_voices, false, &Voice::active);
    if (slot == m_voices.end()) {
        slot = std::ranges::min_element(m_voices, [](const Voice& x, const Voice& y) {
            return x.priority != y.priority ? x.priority < y.priority : x.startOrder < y.startOrder;
        });
        if (slot->priority > command.params.priority) {
            m_dropped.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        m_stolen.fetch_add(1, std::memory_order_relaxed);
    }
    const auto index = static_cast<std::size_t>(slot - m_voices.begin());
    Voice& voice = *slot;
    voice = Voice{};
    voice.id = command.voice;
    voice.sound = command.sound;
    voice.stream = command.stream;
    voice.bus = command.params.bus;
    voice.priority = command.params.priority;
    voice.startOrder = m_startOrder++;
    voice.paused = command.params.paused;
    voice.volume = clampFinite(command.params.volume, 0.0F, 1.0F);
    voice.pan = clampFinite(command.params.pan, -1.0F, 1.0F);
    voice.pitch = clampFinite(command.params.pitch, kMinPitch, kMaxPitch);
    updateGains(voice);
    updateStep(voice);
    // The first two frames: the play position starts exactly on frame 0. A stream with nothing yet starts silent.
    if (!fetch(voice, voice.a)) {
        m_slotIds.at(index).store(0, std::memory_order_release);
        return; // a finished, empty stream: nothing to play
    }
    voice.bPastEnd = !fetch(voice, voice.b);
    voice.active = true;
    // Published last, so a game thread that sees the id also sees the play applied.
    m_slotIds.at(index).store(voice.id, std::memory_order_release);
}

// Ends the voice in `slot`; the game side may then let its sound go.
void Mixer::endVoice(std::size_t slot) {
    Voice& voice = m_voices.at(slot);
    voice.active = false;
    voice.sound = nullptr;
    voice.stream = nullptr;
    m_slotIds.at(slot).store(0, std::memory_order_release);
}

// The voice's Q15 gains from its volume and pan, its bus's volume and the master volume. The pan law is a balance,
// **Coney's choice**: centred, both sides play at full volume (no -3 dB dip), and moving towards one side only
// lowers the other.
void Mixer::updateGains(Voice& voice) const {
    const float level = voice.volume * m_mixBusVolume.at(static_cast<std::size_t>(voice.bus)) * m_mixMasterVolume;
    const float left = voice.pan > 0.0F ? 1.0F - voice.pan : 1.0F;
    const float right = voice.pan < 0.0F ? 1.0F + voice.pan : 1.0F;
    voice.gainLeft = toQ15(level * left);
    voice.gainRight = toQ15(level * right);
}

// The resampler's step: source frames per output frame, from the source's rate and the pitch, in Q32.32.
void Mixer::updateStep(Voice& voice) const {
    const int rate = voice.sound != nullptr ? voice.sound->sampleRate() : voice.stream->sampleRate();
    const double step = static_cast<double>(rate) * static_cast<double>(voice.pitch) / kOutputRate;
    voice.step = static_cast<std::uint64_t>(std::llround(step * static_cast<double>(kPhaseOne)));
}

// The voice's next source frame into `frame` (a mono frame on both sides); false past a sound's end (not looping) or
// once a finished stream is drained. A stream that has run dry but not finished gives silence (an underrun).
bool Mixer::fetch(Voice& voice, std::array<std::int32_t, 2>& frame) {
    if (voice.stream != nullptr) {
        const std::optional<std::array<std::int16_t, 2>> read = voice.stream->readFrame();
        if (read) {
            frame = {(*read)[0], (*read)[1]};
            return true;
        }
        frame = {0, 0};
        return !voice.stream->drained();
    }
    const PcmSound& sound = *voice.sound;
    const std::optional<LoopPoints>& loop = sound.loop();
    const std::uint32_t end = loop ? loop->end : sound.frames();
    if (voice.next >= end) {
        if (!loop) {
            frame = {0, 0};
            return false;
        }
        voice.next = loop->start;
    }
    const std::span<const std::int16_t> samples = sound.samples();
    if (sound.channels() == 2) {
        frame = {samples[std::size_t{voice.next} * 2], samples[(std::size_t{voice.next} * 2) + 1]};
    } else {
        frame = {samples[voice.next], samples[voice.next]};
    }
    ++voice.next;
    return true;
}

// Moves the play position on by one source frame; false when the voice has played its last frame.
bool Mixer::advance(Voice& voice) {
    if (voice.bPastEnd) {
        return false;
    }
    voice.a = voice.b;
    voice.bPastEnd = !fetch(voice, voice.b);
    return true;
}

// Adds `frames` output frames of the voice in `slot` to the accumulator: linear interpolation between the frames
// either side of the play position (**Coney's choice**; the PS2's sound processor interpolates with four taps), then
// the Q15 gains. Integer only, so the result is the same on every machine.
void Mixer::render(Voice& voice, std::size_t slot, std::size_t frames) {
    for (std::size_t f = 0; f < frames; ++f) {
        const auto fraction = static_cast<std::int64_t>(voice.phase >> 16); // Q16
        const auto left = static_cast<std::int32_t>(voice.a[0] + (((voice.b[0] - voice.a[0]) * fraction) >> 16));
        const auto right = static_cast<std::int32_t>(voice.a[1] + (((voice.b[1] - voice.a[1]) * fraction) >> 16));
        m_accumulator.at(f * 2) += (left * voice.gainLeft) >> 15;
        m_accumulator.at((f * 2) + 1) += (right * voice.gainRight) >> 15;
        voice.phase += voice.step;
        while (voice.phase >= kPhaseOne) {
            voice.phase -= kPhaseOne;
            if (!advance(voice)) {
                endVoice(slot);
                return;
            }
        }
    }
}

void Mixer::mix(std::span<std::int16_t> out) {
    CONEY_ASSERT(out.size() % kChannels == 0);
    applyCommands();
    const std::size_t frames = out.size() / kChannels;
    for (std::size_t done = 0; done < frames;) {
        const std::size_t count = std::min(kMixChunk, frames - done);
        std::fill_n(m_accumulator.begin(), count * kChannels, 0);
        for (std::size_t slot = 0; slot < m_voices.size(); ++slot) {
            Voice& voice = m_voices.at(slot);
            if (voice.active && !voice.paused) {
                render(voice, slot, count);
            }
        }
        // Clip the sum to 16 bits: loud mixes saturate rather than wrap round.
        for (std::size_t i = 0; i < count * kChannels; ++i) {
            out[(done * kChannels) + i] = static_cast<std::int16_t>(
                std::clamp<std::int32_t>(m_accumulator.at(i), std::numeric_limits<std::int16_t>::min(),
                                         std::numeric_limits<std::int16_t>::max()));
        }
        done += count;
    }
    m_framesMixed.fetch_add(frames, std::memory_order_relaxed);
}

} // namespace coney::audio
