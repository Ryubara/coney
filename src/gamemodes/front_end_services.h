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

/// What the front end asks of the audio manager (music tracks, front-end sound cues), the movie player and the script
/// system's calls of Lua functions, in one place. Each request is recorded (for tests) and logged:
///
/// - **Music and cues** are not played: there is no audio yet. The current track is remembered, so "play `menu` if it
///   is not already playing" behaves as in the original.
/// - **Movies** go to the movie player attached with attachMoviePlayer(); without one each is skipped, as if it had
///   ended at once: Coney has no video decoder yet (Coney's choice; the original blocks until the movie ends).
/// - **Lua calls** go to the script system attached with attachScripts(); without one (or before its state exists)
///   they are skipped.
///
/// Nothing in the original corresponds; every request names the original's call in the caller's comments.
class FrontEndServices final : public MoviePlayer {
  public:
    /// Writes one line per request to `log`; an empty log writes nothing.
    explicit FrontEndServices(std::function<void(std::string_view)> log = {});

    /// Starts music track `track` (`menu`), replacing the current one.
    void playMusic(std::string_view track);
    /// Whether `track` is the current music track.
    [[nodiscard]] bool musicPlaying(std::string_view track) const { return m_music == track; }
    /// The current music track; empty when none was started.
    [[nodiscard]] const std::string& music() const { return m_music; }
    /// Stops the current music track (`SoundStopMusicTrack`).
    void stopMusic();

    /// Plays front-end sound cue `cue` (an entry of the audio manager's table, docs/research/frontend.md#audio-cues).
    void playCue(int cue);
    /// The cues asked for, in order.
    [[nodiscard]] const std::vector<int>& cues() const { return m_cues; }

    /// Plays the movie `name` (`LOGO`, `L1_IN`, `L99_IN`) through the attached movie player, or skips it.
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

  private:
    // Writes `line` to the log, if there is one.
    void write(const std::string& line) const;

    std::function<void(std::string_view)> m_log;
    std::string m_music;
    std::vector<int> m_cues;
    std::vector<std::string> m_movies;
    std::vector<std::string> m_scriptCalls;
    script::ScriptSystem* m_scripts = nullptr; // where Lua calls go; not owned
    MoviePlayer* m_moviePlayer = nullptr;      // where movies go; not owned
};

} // namespace coney
