// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "gamemodes/movie_player.h"

namespace coney {

namespace script {
class ScriptSystem;
} // namespace script

/// The start-up movies `main` plays before it pushes the first game modes, in order
/// (docs/research/boot.md#main, step 7).
inline constexpr std::array<std::string_view, 3> kStartUpMovies{"LOGO", "PLOGO", "L1_IN"};

/// What the front end asks of the audio (docs/research/sound.md): a sound bank loaded into sound RAM
/// (`AudioManager_LoadBank`, `0x0010fa50`: `menu` at the front end, the interface cues), a music track looped or
/// stopped (`SoundLoopMusicTrack`: `music/wonderwheel_132b`, `MenuTrack`), and front-end sound cue `n` (entry `n` of
/// the loaded bank's table, `0x0010fc30`). The audio backend (audio/sound_player.h) implements it; a run without audio
/// has none.
class FrontEndAudio {
  public:
    virtual ~FrontEndAudio() = default;
    FrontEndAudio() = default;
    FrontEndAudio(const FrontEndAudio&) = delete;
    FrontEndAudio& operator=(const FrontEndAudio&) = delete;
    FrontEndAudio(FrontEndAudio&&) = delete;
    FrontEndAudio& operator=(FrontEndAudio&&) = delete;

    /// Makes `bank` the bank in sound RAM.
    virtual void loadBank(std::string_view bank) = 0;
    /// Starts music track `track` (a name such as `music/wonderwheel_132b`), replacing the current one.
    virtual void playMusic(std::string_view track) = 0;
    /// Stops the music.
    virtual void stopMusic() = 0;
    /// Plays front-end sound cue `cue` of the loaded bank.
    virtual void playCue(int cue) = 0;
};

/// The front end's requests of the audio, the movie player and the script system's calls of Lua functions, in one
/// place. Each request is recorded (for tests), logged and passed on:
///
/// - **Banks, music and cues** go to the FrontEndAudio attached with attachAudio(); without one they are only recorded.
///   The current bank and track are remembered, so "load `menu` unless it is current" behaves as in the original.
/// - **Movies** go to the movie player attached with attachMoviePlayer(); without one each is skipped, as if it had
///   ended at once (the unit tests' runs; the original blocks until the movie ends). The music stops first either way,
///   as `Movie_Play` stops it; with a movie playing, movies::MovieMode stops every other sound.
/// - **Lua calls** go to the script system attached with attachScripts(); without one (or before its state exists)
///   they are skipped.
///
/// Nothing in the original corresponds; every request names the original's call in the caller's comments.
class FrontEndServices final : public MoviePlayer {
  public:
    /// Writes one line per request to `log`; an empty log writes nothing.
    explicit FrontEndServices(std::function<void(std::string_view)> log = {});

    /// Loads sound bank `bank` (`menu`) into sound RAM.
    /// @orig 0x0010fa50 AudioManager_LoadBank (unknown)
    void loadBank(std::string_view bank);
    /// Whether `bank` is the bank in sound RAM.
    [[nodiscard]] bool bankLoaded(std::string_view bank) const { return m_bank == bank; }
    /// The bank in sound RAM; empty when none was loaded.
    [[nodiscard]] const std::string& bank() const { return m_bank; }

    /// Starts music track `track`, replacing the current one (`SoundLoopMusicTrack`, `SoundPlayMusicTrack`).
    void playMusic(std::string_view track);
    /// Whether `track` is the current music track.
    [[nodiscard]] bool musicPlaying(std::string_view track) const { return m_music == track; }
    /// The current music track; empty when none plays.
    [[nodiscard]] const std::string& music() const { return m_music; }
    /// Stops the current music track (`SoundStopMusicTrack`).
    void stopMusic();

    /// Plays front-end sound cue `cue` (an entry of the loaded bank's table, docs/research/frontend.md#audio-cues).
    /// @orig 0x0010fc30 AudioManager_PlayFrontEndSound (unknown)
    void playCue(int cue);
    /// The cues asked for, in order.
    [[nodiscard]] const std::vector<int>& cues() const { return m_cues; }

    /// Plays the movie `name` (`LOGO`, `L1_IN`, `L99_IN`) through the attached movie player, or skips it; the music
    /// stops first.
    void playMovie(std::string_view name) override;
    /// Sends movies to `player` (null: skip them). The player must outlive its use here.
    void attachMoviePlayer(MoviePlayer* player) { m_moviePlayer = player; }
    /// The movies asked for, in order.
    [[nodiscard]] const std::vector<std::string>& movies() const { return m_movies; }

    /// Calls the Lua function `function` (a dotted name such as `Menu.playMovie`) with numeric `args` through the
    /// attached script system; skipped without one.
    void callScript(std::string_view function, std::span<const double> args = {});
    /// Sends Lua calls to `scripts` (null: skip them). The script system must outlive its use here.
    void attachScripts(script::ScriptSystem* scripts) { m_scripts = scripts; }
    /// The Lua functions asked for, in order.
    [[nodiscard]] const std::vector<std::string>& scriptCalls() const { return m_scriptCalls; }

    /// Sends banks, music and cues to `audio` (null: record them only). It must outlive its use here.
    void attachAudio(FrontEndAudio* audio) { m_audio = audio; }

  private:
    // Writes `line` to the log, if there is one.
    void write(const std::string& line) const;

    std::function<void(std::string_view)> m_log;
    std::string m_bank;
    std::string m_music;
    std::vector<int> m_cues;
    std::vector<std::string> m_movies;
    std::vector<std::string> m_scriptCalls;
    script::ScriptSystem* m_scripts = nullptr; // where Lua calls go; not owned
    FrontEndAudio* m_audio = nullptr;          // where banks, music and cues go; not owned
    MoviePlayer* m_moviePlayer = nullptr;      // where movies go; not owned
};

} // namespace coney
