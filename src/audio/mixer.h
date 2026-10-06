// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "audio/audio_format.h"
#include "audio/pcm_sound.h"
#include "audio/pcm_stream.h"
#include "audio/spsc_queue.h"

namespace coney::audio {

/// The slowest and fastest a voice may play, as a multiple of its sound's rate. **Coney's choice**: a range far wider
/// than pitch bends need, which keeps the resampler's step in range.
inline constexpr float kMinPitch = 1.0F / 64.0F;
inline constexpr float kMaxPitch = 8.0F;

/// How a voice starts playing. Volumes are linear amplitude; every value is clamped to its range.
struct VoiceParams {
    Bus bus = Bus::Sfx;        ///< The bus whose volume (under the master volume) scales the voice.
    float volume = 1.0F;       ///< 0 (silent) to 1 (as recorded).
    float pan = 0.0F;          ///< -1 (left only) to 1 (right only); 0 plays both sides at full volume.
    float pitch = 1.0F;        ///< The playback rate as a multiple of the sound's rate, kMinPitch to kMaxPitch.
    std::uint8_t priority = 0; ///< Higher keeps its voice: with every voice busy, a play steals the lowest.
    bool paused = false;       ///< Start paused (held at its first frame until resumed).
};

/// A voice a play started, to change or stop it later. A handle stays harmless once its voice has ended: commands on it
/// do nothing and isPlaying() says false. The zero handle is never a voice's.
struct VoiceHandle {
    std::uint32_t id = 0;
    /// Whether the play was accepted at all (a full command queue refuses it).
    [[nodiscard]] bool valid() const { return id != 0; }
    friend bool operator==(VoiceHandle, VoiceHandle) = default;
};

/// What the mixer has done, for logs, the debug menu and tests.
struct MixerStats {
    std::uint64_t framesMixed = 0;     ///< Output frames produced since the mixer was made.
    std::uint64_t voicesStolen = 0;    ///< Plays that took a busy voice from a lower or equal priority one.
    std::uint64_t playsDropped = 0;    ///< Plays that found every voice busy with higher priorities.
    std::uint64_t commandsDropped = 0; ///< Commands lost to a full queue (nothing pulled the mixer for a long time).
};

/// Coney's software mixer: up to kVoiceCount voices of PCM (whole sounds or streams, any rate, mono or stereo) mixed
/// into signed 16-bit stereo at kOutputRate, with a volume, pan and pitch per voice, looping by the sound's loop
/// points, priorities and voice stealing, and a volume per Bus under a master volume. It knows nothing of the game's
/// sound formats or of any device: the game's sound engine sits on top of it, and a device (SDL's, or the offline one
/// of test mode) pulls from it.
///
/// **Two threads.** The game thread calls everything but mix(): each call becomes a command in a lock-free queue
/// (SpscQueue: the device's callback must never wait on the game). mix() runs on the device's thread (or the game's,
/// offline): it applies the queued commands, then renders. Only one thread may be the game side, and one the mix side.
///
/// **Determinism.** The mix path is integer arithmetic only (Q32.32 resampler phase, Q15 gains, a 32-bit accumulator
/// clipped to 16 bits), with gains and steps worked out from the floats once per command, so pulling the same frames
/// after the same commands gives the same samples on every run.
///
/// **No allocation on the mix path**: voices, the queue and the accumulator are fixed arrays. The sounds a voice plays
/// stay alive through the shared pointers the game side keeps until collect() sees the voice has ended, so the mix
/// thread never releases memory either.
///
/// Research: docs/research/sound.md#coneys-implementation (the game's own engine is not traced yet).
class Mixer {
  public:
    Mixer();
    ~Mixer();
    Mixer(const Mixer&) = delete;
    Mixer& operator=(const Mixer&) = delete;
    Mixer(Mixer&&) = delete;
    Mixer& operator=(Mixer&&) = delete;

    // ---- Game thread ----

    /// Plays `sound` (which must not be null) with `params`; the voice starts at the next mix(). Returns an invalid
    /// handle when the command queue is full.
    VoiceHandle play(std::shared_ptr<const PcmSound> sound, const VoiceParams& params = {});
    /// Plays `stream` (which must not be null) with `params`, as play() does a sound.
    VoiceHandle play(std::shared_ptr<PcmStream> stream, const VoiceParams& params = {});
    /// Stops the voice at once.
    void stop(VoiceHandle voice);
    /// Stops every voice.
    void stopAll();
    /// Sets a playing voice's volume (0 to 1).
    void setVolume(VoiceHandle voice, float volume);
    /// Sets a playing voice's pan (-1 to 1).
    void setPan(VoiceHandle voice, float pan);
    /// Sets a playing voice's pitch (kMinPitch to kMaxPitch).
    void setPitch(VoiceHandle voice, float pitch);
    /// Pauses a voice where it is, or resumes it.
    void setPaused(VoiceHandle voice, bool paused);
    /// Pauses every voice playing now where it is, remembering which, as the pause mode does on entry; voices started
    /// afterwards (the pause menu's sounds) play normally. A second call adds the voices started since.
    /// Resuming is the same function's other half (0x0010fb68), resumeAll().
    /// @orig 0x0010fb20 SoundPauseSound (unknown)
    /// Research: docs/research/sound.md#volumes-and-the-options, docs/research/pause.md#pausing
    void pauseAll();
    /// Resumes exactly the voices pauseAll() paused; one paused on its own with setPaused() stays paused.
    void resumeAll();
    /// Sets the volume of `bus` (0 to 1), for every voice on it.
    void setBusVolume(Bus bus, float volume);
    /// The volume last set for `bus` (1 at the start).
    [[nodiscard]] float busVolume(Bus bus) const;
    /// Sets the master volume (0 to 1), over every bus.
    void setMasterVolume(float volume);
    /// The master volume last set (1 at the start).
    [[nodiscard]] float masterVolume() const { return m_masterVolume; }

    /// Whether the voice is waiting to start or playing (paused counts as playing).
    [[nodiscard]] bool isPlaying(VoiceHandle voice) const;
    /// How many voices are playing now, as of the last mix().
    [[nodiscard]] std::size_t voicesPlaying() const;
    /// Lets go of the sounds and streams of voices that have ended. Call it once a step or so; until then they stay
    /// alive (which is what keeps them safe for the mix thread).
    void collect();
    /// How many sounds and streams the game side still holds for voices (playing, queued or not yet collected).
    [[nodiscard]] std::size_t held() const { return m_held.size(); }
    /// The counts so far.
    [[nodiscard]] MixerStats stats() const;

    // ---- Mix thread ----

    /// Applies the queued commands, then fills `out` (interleaved stereo, an even number of samples) with the next
    /// frames of every playing voice, summed and clipped.
    void mix(std::span<std::int16_t> out);

  private:
    // What a command asks the mix thread to do.
    enum class CommandKind : std::uint8_t {
        Play,
        Stop,
        StopAll,
        Volume,
        Pan,
        Pitch,
        Pause,
        PauseAll,
        ResumeAll,
        BusVolume,
        MasterVolume
    };

    // One command from the game thread. Plain data: the pointers stay valid through m_held.
    struct Command {
        CommandKind kind = CommandKind::Stop;
        std::uint32_t voice = 0;
        std::uint64_t sequence = 0;
        float value = 0.0F;
        bool flag = false;
        Bus bus = Bus::Sfx;
        VoiceParams params{};
        const PcmSound* sound = nullptr;
        PcmStream* stream = nullptr;
    };

    // A sound or stream the game side keeps alive for a voice.
    struct Held {
        std::uint32_t voice = 0;
        std::uint64_t sequence = 0; // of the play command
        std::shared_ptr<const PcmSound> sound{};
        std::shared_ptr<PcmStream> stream{};
    };

    // One voice, owned by the mix thread.
    struct Voice {
        bool active = false;
        std::uint32_t id = 0;
        const PcmSound* sound = nullptr;
        PcmStream* stream = nullptr;
        Bus bus = Bus::Sfx;
        std::uint8_t priority = 0;
        std::uint64_t startOrder = 0;
        bool paused = false;
        bool pausedByAll = false; // by pauseAll(), apart from paused so resumeAll() leaves setPaused() alone
        float volume = 1.0F;
        float pan = 0.0F;
        float pitch = 1.0F;
        std::int32_t gainLeft = 0;       // Q15, with the bus and master volumes
        std::int32_t gainRight = 0;      // Q15
        std::uint64_t step = 0;          // source frames per output frame, Q32.32
        std::uint64_t phase = 0;         // how far between frames a and b, Q0.32
        std::array<std::int32_t, 2> a{}; // the source frame at or before the play position
        std::array<std::int32_t, 2> b{}; // the frame after it
        bool bPastEnd = false;           // b is beyond a sound's end or a finished stream
        std::uint32_t next = 0;          // a sound's next frame to fetch
    };

    // Output frames rendered per pass through the voices: the accumulator's size.
    static constexpr std::size_t kMixChunk = 256;
    static constexpr std::size_t kQueueSize = 1024;

    VoiceHandle startPlay(Command command, Held held);
    void send(const Command& command);
    void sendVoice(CommandKind kind, VoiceHandle voice, float value, bool flag = false);
    [[nodiscard]] bool slotHolds(std::uint32_t voice) const;

    void applyCommands();
    void apply(const Command& command);
    void startVoice(const Command& command);
    void endVoice(std::size_t slot);
    void updateGains(Voice& voice) const;
    void updateStep(Voice& voice) const;
    [[nodiscard]] Voice* find(std::uint32_t id);
    bool fetch(Voice& voice, std::array<std::int32_t, 2>& frame);
    bool advance(Voice& voice);
    void render(Voice& voice, std::size_t slot, std::size_t frames);

    // Game side.
    std::uint32_t m_nextId = 1;
    std::uint64_t m_nextSequence = 1;
    std::vector<Held> m_held;
    std::array<float, kBusCount> m_busVolume{};
    float m_masterVolume = 1.0F;
    std::uint64_t m_commandsDropped = 0;

    // Shared: the queue, what the mix thread has applied and which voice each slot plays (0: none).
    SpscQueue<Command, kQueueSize> m_queue;
    std::atomic<std::uint64_t> m_applied{0};
    std::array<std::atomic<std::uint32_t>, kVoiceCount> m_slotIds{};
    std::atomic<std::uint64_t> m_framesMixed{0};
    std::atomic<std::uint64_t> m_stolen{0};
    std::atomic<std::uint64_t> m_dropped{0};

    // Mix side.
    std::array<Voice, kVoiceCount> m_voices{};
    std::array<float, kBusCount> m_mixBusVolume{};
    float m_mixMasterVolume = 1.0F;
    std::uint64_t m_startOrder = 0;
    std::array<std::int32_t, kMixChunk * 2> m_accumulator{};
};

} // namespace coney::audio
