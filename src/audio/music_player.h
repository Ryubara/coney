// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>

#include "audio/mixer.h"
#include "audio/sound_stream.h"

namespace coney::audio {

class SoundTables;

/// The states of a music channel, as the game names them (strings at 0x00546da8).
enum class MusicState : std::uint8_t {
    Idle,       ///< ST_Idle: nothing.
    Queued,     ///< ST_Queued: a request waiting for a stream pair.
    PreLoading, ///< ST_PreLoading: the stream fills before it starts.
    Playing,    ///< ST_Playing.
    FadeIn,     ///< ST_FadeIn.
    Blocked,    ///< ST_Blocked: not reached in Coney (its use is not traced).
    BarSync,    ///< ST_MSPMSync: waiting for the playing track's next bar to start.
    FadeOut,    ///< ST_FadeOut: fading to silence, then stopped.
};

/// The game's music player (docs/research/sound.md#music): tracks of MUSIC.SND played on two stereo stream pairs (1+2
/// and 3+4), switching on **bar boundaries** with cross-fades, as scripted tracks or as the **system music** that
/// follows the mood of the fight. Three channels: two that play and the queued request.
///
/// Game thread only. Its clock is the sound engine's.
class MusicPlayer {
  public:
    /// The game's default bar length of a track the scripts have not configured, ms (MusicList_Load's 2000).
    static constexpr float kDefaultBarMs = 2000.0F;
    /// How long before a bar boundary a synchronised track starts, ms.
    static constexpr float kBarLeadMs = 33.0F;
    /// How many moods the system music has (0 idle, 1 fight, 2 search; meanings inferred).
    static constexpr int kMoods = 3;
    /// Tracks each mood can have.
    static constexpr std::size_t kTracksPerMood = 3;

    /// A player over `mixer`, reading tracks from `tables` (whose records it may change through configure()).
    MusicPlayer(Mixer& mixer, SoundTables& tables);
    ~MusicPlayer();
    MusicPlayer(const MusicPlayer&) = delete;
    MusicPlayer& operator=(const MusicPlayer&) = delete;
    MusicPlayer(MusicPlayer&&) = delete;
    MusicPlayer& operator=(MusicPlayer&&) = delete;

    /// Sets a track's bar length and volume (SndCfgMusicInfo(track, bar, volume)).
    void configure(std::uint32_t hash, float barMs, float volume);
    /// Queues the track `hash` (docs/research/sound.md#music): it starts at once when nothing plays, else on the
    /// playing track's next bar, cross-fading over `fadeBars` bars (0: a cut). It loops when `loop`; otherwise when it
    /// ends `callback` (a script function's name; empty for none) is told. Nothing happens while music is not allowed
    /// or for a track not in the music list.
    /// @orig 0x0010d8e8 Music_Play (unknown)
    void play(std::uint32_t hash, bool loop, std::string callback = {}, int fadeBars = 0);
    /// Fades out or stops each channel by its state (SoundStopMusicTrack).
    /// @orig 0x0010d9a0 Music_Stop (unknown)
    void stop();
    /// Runs the channels at the clock `now` (ms): starts, bar syncs, fades, ends; then feeds the streams and sets the
    /// volumes. The system music first, when it is on.
    /// @orig 0x0010dfe0 MusicChannel_Update (unknown)
    void update(double now, SoundFiles* files, const std::function<std::int32_t(std::int32_t, std::int32_t)>& random);

    /// The scripts' music volume (SoundSetMusicVolume, 1).
    void setVolume(float volume) { m_volume = volume; }
    /// The options' music volume (0.9 by default).
    void setOptionsVolume(float volume) { m_optionsVolume = volume; }
    [[nodiscard]] float optionsVolume() const { return m_optionsVolume; }
    /// Whether the game state lets music play (false: play() does nothing).
    void setAllowed(bool allowed) { m_allowed = allowed; }
    /// Whether a scene plays (game state +0x410): the music ducks while it does.
    void setScenePlaying(bool playing) { m_scenePlaying = playing; }
    /// Turns ducking under scenes on or off and sets its factor (SndEnableMusicDuck, 0.75).
    void setSceneDuck(bool enabled, float factor = 0.75F);
    /// Pauses the music's voices or resumes them.
    void setPaused(bool paused);

    /// Sets the up-to-three tracks of a mood (SoundSetMusicTrack(mood, a, b, c)); a 0 hash is no track.
    void setSystemTracks(int mood, std::array<std::uint32_t, kTracksPerMood> tracks);
    /// Turns the system music on or off (game state +0x3f8).
    void setSystemMusic(bool on) { m_systemMusic = on; }
    /// Sets the mood (game state +0x40c: SoundSetSystemMusicState).
    void setMood(int mood) { m_mood = mood; }
    /// Makes the system music choose a track again at the next update even if the mood is the same.
    void forceMoodChange() { m_forceMood = true; }
    /// Told the name of a track's callback when a track played once ends.
    void setTrackEndCallback(std::function<void(std::string_view)> callback) { m_onTrackEnd = std::move(callback); }
    /// Told the new mood when the system music changes it (SoundSetMusicStateCallback).
    void setMoodCallback(std::function<void(int)> callback) { m_onMood = std::move(callback); }

    /// The state of channel 0, 1 (the two that play) or 2 (the request).
    [[nodiscard]] MusicState state(std::size_t channel) const { return m_channels.at(channel).state; }
    /// The track of channel `channel` (0 when idle).
    [[nodiscard]] std::uint32_t track(std::size_t channel) const { return m_channels.at(channel).hash; }
    /// The volume last sent for channel `channel`.
    [[nodiscard]] float sentVolume(std::size_t channel) const { return m_channels.at(channel).sent; }
    /// How many stream reads of MUSIC.SND failed.
    [[nodiscard]] std::uint64_t readErrors() const { return m_readErrors; }
    /// Whether a channel holds stereo pair slot `slot` (0: pair 1+2, 1: pair 3+4), which the music shares with the
    /// stereo sounds (docs/research/sound.md#stream-pairs).
    [[nodiscard]] bool holdsPair(std::size_t slot) const {
        return slot < 2 && m_channels.at(slot).state != MusicState::Idle;
    }
    /// Asks whether a queued track may take the free slot `slot` from the stereo sounds: with `takeOver` false only
    /// when no stereo sound holds it, with it true also by taking it over from a prepared soundtrack.
    using PairGate = std::function<bool(std::size_t slot, bool takeOver)>;
    /// Sets the gate (empty: every idle slot is the music's).
    void setPairGate(PairGate gate) { m_pairGate = std::move(gate); }
    /// The track playing or fading in (0 for none): the one a new request syncs to.
    [[nodiscard]] std::uint32_t currentTrack() const;

  private:
    // One channel: the game's 0x4c-byte music channel.
    struct Channel {
        MusicState state = MusicState::Idle;
        std::uint32_t hash = 0;
        int pair = 0; // stream pair 1 or 3
        bool loop = false;
        int fadeBars = 0;
        float barMs = kDefaultBarMs;
        float trackVolume = 1.0F;
        double fadeStart = 0.0;
        double playStart = 0.0;
        double nextBar = 0.0;
        double fadeLength = 0.0;
        float fade = 0.0F;
        float sent = -1.0F;
        std::string callback;
        std::unique_ptr<StreamFeeder> feeder;
        VoiceHandle voice;
    };

    void updateSystemMusic(const std::function<std::int32_t(std::int32_t, std::int32_t)>& random);
    void startPreload(Channel& channel, SoundFiles* files);
    void start(Channel& channel, double now, bool fadeIn);
    void fadeOut(Channel& channel, double now, float lengthMs);
    void halt(Channel& channel);
    [[nodiscard]] bool active(const Channel& channel) const;

    Mixer& m_mixer;
    SoundTables& m_tables;
    std::array<Channel, 3> m_channels;
    std::map<std::uint32_t, float> m_barMs; // the configured bar lengths, by track
    double m_now = 0.0;
    float m_volume = 1.0F;
    float m_optionsVolume = 0.9F;
    bool m_allowed = true;
    bool m_scenePlaying = false;
    bool m_duckEnabled = true;
    float m_duck = 0.75F;
    bool m_paused = false;
    bool m_systemMusic = false;
    int m_mood = 0;
    int m_playingMood = -1;
    bool m_forceMood = false;
    std::array<std::array<std::uint32_t, kTracksPerMood>, kMoods> m_moodTracks{};
    std::function<void(std::string_view)> m_onTrackEnd;
    std::function<void(int)> m_onMood;
    PairGate m_pairGate;
    std::uint64_t m_readErrors = 0;
};

} // namespace coney::audio
