// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "audio/mixer.h"
#include "audio/music_player.h"
#include "audio/sound_bank.h"
#include "audio/sound_data.h"
#include "audio/sound_stream.h"
#include "core/error.h"

namespace coney::audio {

/// A point or direction in the world, metres.
struct SoundVec {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

/// Where a listener hears from: its position and its side axis (to the listener's right), along which its two ears sit
/// half a metre either side. The game keeps two, one per player.
struct Listener {
    SoundVec position{};
    SoundVec right{1.0F, 0.0F, 0.0F};
};

/// A sound the engine started, to change or stop it. It stays harmless once the sound has ended.
struct SoundHandle {
    std::uint32_t id = 0;
    [[nodiscard]] bool valid() const { return id != 0; }
    friend bool operator==(SoundHandle, SoundHandle) = default;
};

/// How a play call starts a sound: the arguments of the game's AudioManager_Play (docs/research/sound.md#play).
struct SoundPlay {
    float volume = 1.0F;       ///< The caller's volume factor.
    float pitch = 1.0F;        ///< The caller's pitch factor (1 for a non-duckable sound).
    float volumeFactor = 1.0F; ///< The third volume factor (`a8`; what each caller uses it for is open).
    float panLeft = 1.0F;      ///< A 2D sound's left gain.
    float panRight = 1.0F;     ///< A 2D sound's right gain.
    float fadeInMs = 0.0F;     ///< Fade in over this many ms (0: at once).
    bool duckable = true;      ///< false: a sound that ducks the directional ones (the play call's last argument).
    std::uint32_t owner = 0;   ///< Who plays it (0: nobody); see SoundEngine::setPlayerOwner().
    std::optional<SoundVec> position{}; ///< A positional sound's place; none plays it as a 2D sound.
    SoundVec facing{0.0F, 0.0F, 1.0F};  ///< A directional sound's facing.
};

/// What the engine has done, for logs, tests and the debug menu.
struct SoundEngineStats {
    std::size_t tasks = 0;            ///< Live tasks now.
    std::size_t virtualTasks = 0;     ///< Of them, playing virtually (tracked, silent).
    std::uint64_t started = 0;        ///< Tasks started.
    std::uint64_t refused = 0;        ///< Plays refused by admission.
    std::uint64_t unknown = 0;        ///< Plays of a hash with no sound.
    std::uint64_t stolen = 0;         ///< Voices taken from a less important task.
    std::uint64_t missingSamples = 0; ///< Bank sounds whose sample is not in the loaded bank.
    std::uint64_t readErrors = 0;     ///< Stream reads that failed.
    std::uint64_t bankErrors = 0;     ///< Banks the load screen could not load.
};

/// The game's audio manager: every sound is a task (up to kMaxTasks) started by name hash. The sound list gives its
/// sample and class; the class its priority, distances and flags. A task takes one of the game's 48 voices: 13-47 for
/// samples of the bank loaded into sound RAM, the stream channels 5-9 (10-12 for the small loops) for sounds streamed
/// from BFW.SND, or plays **virtually** (tracked, silent, its length timed) when none is free and none can be stolen.
/// Each update a task's left and right volumes are worked out from its distance to the listeners and a two-ear pan,
/// times the record's volume, the options' sound volume and fades, and its rate from the record's rate and the pitch
/// factors; both go to the Mixer as the game sends them to its device. Music is the MusicPlayer's.
///
/// Game thread only; deterministic for test mode (the clock is the steps passed to update(), the random factors are
/// drawn from the random source given at construction).
///
/// Research: docs/research/sound.md
class SoundEngine {
  public:
    /// The game's task pool size.
    static constexpr std::size_t kMaxTasks = 256;
    /// Draws a whole number in [low, high]; the game's random in a run, any deterministic source in a test.
    using RandomRange = std::function<std::int32_t(std::int32_t low, std::int32_t high)>;
    /// Loads a bank by name (`sound`, `menu`, `load_03`) with the engine's tables, from the disc's WAD in a run.
    using BankLoader = std::function<std::expected<SoundBank, Error>(std::string_view name, const SoundTables& tables)>;

    /// An engine over `mixer` with the game's `tables`, streaming from `files` (which may be null: streamed sounds then
    /// play virtually) and loading banks with `loadBank` (which may be empty: no bank ever loads).
    /// @orig 0x0010f618 AudioManager_Reset (unknown)
    SoundEngine(Mixer& mixer, SoundTables tables, std::unique_ptr<SoundFiles> files, BankLoader loadBank,
                RandomRange random);
    ~SoundEngine();
    SoundEngine(const SoundEngine&) = delete;
    SoundEngine& operator=(const SoundEngine&) = delete;
    SoundEngine(SoundEngine&&) = delete;
    SoundEngine& operator=(SoundEngine&&) = delete;

    // ---- Playing ----

    /// Starts the sound `hash` (docs/research/sound.md#play): finds its record, admits it, takes a task and a voice (or
    /// plays it virtually), runs one update of it and starts its voice. An invalid handle when nothing plays (no such
    /// sound, refused, or the task pool is full).
    /// @orig 0x00111de8 AudioManager_Play (unknown)
    SoundHandle play(std::uint32_t hash, const SoundPlay& how = {});
    /// Stops a sound at once, or fades it out over `fadeOutMs`.
    void stop(SoundHandle sound, float fadeOutMs = 0.0F);
    /// Stops every sound.
    void stopAll();
    /// Whether the sound is still live (playing, virtually or for real, or fading).
    [[nodiscard]] bool isPlaying(SoundHandle sound) const;
    /// Whether the sound plays virtually (no voice).
    [[nodiscard]] bool isVirtual(SoundHandle sound) const;
    /// The game voice the sound plays on (1-12 a stream channel, 13-47 an SPU2 voice), or -1.
    [[nodiscard]] int voiceOf(SoundHandle sound) const;
    /// Sets the caller's volume factor of a live sound.
    void setVolume(SoundHandle sound, float volume);
    /// Moves a positional sound.
    void setPosition(SoundHandle sound, SoundVec position, SoundVec facing = {0.0F, 0.0F, 1.0F});
    /// The left and right volumes last sent for the sound (0-1), for tests and the debug menu.
    [[nodiscard]] std::optional<std::array<float, 2>> sentVolumes(SoundHandle sound) const;

    // ---- Each step ----

    /// Advances the clock by `milliseconds` and updates every task with `listeners` (one or two): fades, the 3D volume
    /// and pan, the pitch, the finished freed, the streams fed; then the music.
    /// @orig 0x0010f810 AudioManager_Update (unknown)
    void update(float milliseconds, std::span<const Listener> listeners);
    /// The engine's clock, ms since it was made.
    [[nodiscard]] double now() const { return m_now; }

    // ---- Banks ----

    /// Loads the bank `name` unless it is the current one (docs/research/sound.md#banks); `none` empties sound RAM. A
    /// bank that fails to load leaves none loaded and is returned. While loading is deferred (setDeferBankLoads()) it
    /// only records the name as the pending bank.
    /// @orig 0x0010fa50 AudioManager_LoadBank (unknown)
    std::expected<void, Error> loadBank(std::string_view name);
    /// The current bank's name (`none` for none).
    [[nodiscard]] const std::string& bankName() const { return m_bank.name(); }
    /// The bank to load once loading ends (`none`: the default, `sound`).
    [[nodiscard]] const std::string& pendingBank() const { return m_pendingBank; }
    /// Defers SndLoadBank to the end of loading (game state +0x3fa58; who sets it is open).
    void setDeferBankLoads(bool defer) { m_deferBankLoads = defer; }

    /// The load screen begins (docs/research/sound.md#banks): bank `load_NN` (NN counting 0-6 round from a random
    /// start) or `armload` for the Armies levels, its two halves played hard left and hard right, and new positional
    /// and stereo sounds played virtually until it ends.
    /// @orig 0x00111178 AudioManager_StartLoadScreen (unknown)
    void startLoadScreen(bool armies);
    /// The level has loaded: the load-screen sounds stop and the pending bank (else `sound`) loads; the pending name
    /// goes back to `none` (the bank load is level loading's, `0x0015fe90`, right after the stop).
    /// @orig 0x00111428 AudioManager_StopLoadScreen (unknown)
    void endLoadScreen();

    // ---- Ambience and interface sounds ----

    /// Plays `hash` as the level's ambient bed, looping with a 2 s fade-in, replacing the current one (faded out over
    /// 2 s) unless it is the same.
    /// @orig 0x00110b60 AmbientTrack_Play (unknown)
    void playAmbientTrack(std::uint32_t hash);
    /// Fades the ambient bed out over 2 s.
    /// @orig 0x00110c70 AmbientTrack_Stop (unknown)
    void stopAmbientTrack();
    /// Sets the ambient bed's volume (its caller volume).
    void setAmbientTrackVolume(float volume);
    /// Fills interface cue `index` with the sound `hash` (SoundCfgInterfaceSound).
    void setInterfaceSound(std::size_t index, std::uint32_t hash);
    /// Plays interface cue `index` as a 2D sound; nothing for an empty cue.
    SoundHandle playInterfaceSound(std::size_t index);

    // ---- Scene soundtracks (docs/research/sound.md#scene-sound) ----

    /// Prepares the scene soundtrack `hash` without starting it, so it is buffered before the scene's first frame;
    /// stops the previous one.
    /// @orig 0x0010ff68 SceneSound_Preload (unknown)
    SoundHandle preloadSceneSound(std::uint32_t hash);
    /// Starts the prepared scene soundtrack (scene event 13); false when none is prepared.
    /// @orig 0x00110018 SceneSound_Start (unknown)
    bool startSceneSound();
    /// Stops the scene soundtrack.
    void stopSceneSound();
    /// The scene soundtrack, prepared or playing (invalid when there is none).
    [[nodiscard]] SoundHandle sceneSound() const { return m_sceneSound; }

    // ---- Settings ----

    /// The options' sound-effect volume, 0-1 (default 0.9).
    void setSoundVolume(float volume) { m_soundVolume = volume; }
    [[nodiscard]] float soundVolume() const { return m_soundVolume; }
    /// The global pitch factor (SndSetPitchMod, 1).
    void setPitchFactor(float factor) { m_pitchFactor = factor; }
    /// The duck factor for directional sounds under a non-duckable one (SndSetNIDuck, 0.2).
    void setNonDuckableDuck(float factor) { m_niDuck = factor; }
    /// Whose sounds are a player's: the owner ids that play() calls give for the players' humans.
    void setPlayerOwners(std::vector<std::uint32_t> owners) { m_playerOwners = std::move(owners); }
    /// Pauses every voice (SoundPauseSound): virtual tasks keep their clock; pitches stop changing.
    void pause();
    /// Resumes what pause() paused.
    void resume();
    [[nodiscard]] bool paused() const { return m_paused; }

    /// Draws a whole number in [low, high] from the engine's random source (the emitters' and the voice table's draws).
    std::int32_t random(std::int32_t low, std::int32_t high) { return m_random(low, high); }

    /// The music.
    [[nodiscard]] MusicPlayer& music() { return m_music; }
    /// The sound tables.
    [[nodiscard]] const SoundTables& tables() const { return m_tables; }
    [[nodiscard]] SoundTables& tables() { return m_tables; }
    /// The counts so far.
    [[nodiscard]] SoundEngineStats stats() const;

  private:
    // The game's voices: stream channels 1-12 and SPU2 voices 13-47 (docs/research/sound.md#play).
    static constexpr int kVoiceSlots = 48;
    static constexpr int kFirstStream = 5;
    static constexpr int kLastStream = 9;
    static constexpr int kFirstSmallLoop = 10;
    static constexpr int kLastSmallLoop = 12;
    static constexpr int kFirstSample = 13;
    static constexpr int kLastSample = 47;

    // One sound task (the game's 0xf0-byte SoundTask).
    struct Task {
        bool live = false;
        std::uint32_t id = 0;
        const SoundRecord* record = nullptr;
        const SoundClass* soundClass = nullptr;
        std::uint64_t order = 0; // place in the task list: start order
        int voice = -1;          // game voice, -1 none
        int voice2 = -1;         // a stereo stream's second channel
        bool virtualPlay = false;
        bool prepared = false; // a scene soundtrack prepared, not started
        double lengthMs = 0.0;
        double startMs = 0.0;
        SoundPlay how;
        float randomVolume = 1.0F; // the priority-21 random factor
        float variation = 1.0F;    // the random pitch factor
        float fade = 1.0F;
        int fadeMode = 0; // 1 in, 2 out
        double fadeStart = 0.0;
        double fadeLength = 0.0;
        float distance = 0.0F;
        int state = 1; // 1 playing, 2 faded out, 3 stopped
        std::array<float, 2> sent{-1.0F, -1.0F};
        float rateSent = -1.0F;
        VoiceHandle mixerVoice;
        std::unique_ptr<StreamFeeder> feeder;
    };

    SoundHandle startTask(std::uint32_t hash, const SoundPlay& how, bool prepared);
    Task* find(SoundHandle sound);
    [[nodiscard]] const Task* find(SoundHandle sound) const;
    [[nodiscard]] bool admits(std::uint8_t priority) const;
    bool takeVoice(Task& task);
    Task* findVictim(const Task& task);
    [[nodiscard]] bool voiceFree(int voice) const;
    void startVoice(Task& task);
    void endTask(Task& task);
    void updateTask(Task& task, std::span<const Listener> listeners);
    void positionalVolumes(Task& task, std::span<const Listener> listeners, std::array<float, 2>& volumes) const;
    [[nodiscard]] bool ownedByPlayer(const Task& task) const;
    [[nodiscard]] double durationMs(const Task& task) const;
    [[nodiscard]] float randomFactor(int percent);

    Mixer& m_mixer;
    SoundTables m_tables;
    std::unique_ptr<SoundFiles> m_files;
    BankLoader m_loadBank;
    RandomRange m_random;
    std::vector<Task> m_tasks;                     // kMaxTasks
    std::vector<Listener> m_listeners{Listener{}}; // the last update's; one at the origin to start with
    std::uint32_t m_nextId = 1;
    std::uint64_t m_nextOrder = 0;
    double m_now = 0.0;
    int m_startedThisUpdate = 0;
    bool m_nonDuckablePlaying = false;

    SoundBank m_bank;
    std::string m_pendingBank = "none";
    bool m_deferBankLoads = false;
    int m_loadScreenNumber = 0;
    bool m_loadScreen = false;
    std::array<SoundHandle, 2> m_loadScreenSounds{};

    SoundHandle m_ambient;
    std::uint32_t m_ambientHash = 0;
    float m_ambientVolume = 1.0F;
    std::vector<std::uint32_t> m_interfaceSounds;
    SoundHandle m_sceneSound;

    float m_soundVolume = 0.9F;
    float m_pitchFactor = 1.0F;
    float m_niDuck = 0.2F;
    std::vector<std::uint32_t> m_playerOwners;
    bool m_paused = false;
    SoundEngineStats m_stats;
    MusicPlayer m_music;
};

} // namespace coney::audio
