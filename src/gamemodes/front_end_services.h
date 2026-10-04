// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace coney {

/// The start-up movies `main` plays before it pushes the first game modes, in order
/// (docs/research/boot.md#main, step 7).
inline constexpr std::array<std::string_view, 3> kStartUpMovies{"LOGO", "PLOGO", "L1_IN"};

/// Coney's stand-in for what the front end asks of subsystems Coney does not have yet: the audio manager (music
/// tracks, front-end sound cues), the movie player and the script system's calls of Lua functions. Each request is
/// recorded (for tests and the log) and then skipped:
///
/// - **Music and cues** are not played: there is no audio yet. The current track is remembered, so "play `menu` if it
///   is not already playing" behaves as in the original.
/// - **Movies** are skipped, as if each had ended at once: Coney has no video decoder (Coney's choice; the original
///   blocks until the movie ends).
/// - **Lua calls** are skipped: the level scripts need the script system's level entry and bindings that are not
///   researched yet (docs/research/frontend.md#coneys-implementation).
///
/// Nothing in the original corresponds; every request names the original's call in the caller's comments.
class FrontEndServices {
  public:
    /// Writes one line per request to `log`; an empty log writes nothing.
    explicit FrontEndServices(std::function<void(std::string_view)> log = {});

    /// Starts music track `track` (`menu`), replacing the current one.
    void playMusic(std::string_view track);
    /// Whether `track` is the current music track.
    [[nodiscard]] bool musicPlaying(std::string_view track) const { return m_music == track; }
    /// The current music track; empty when none was started.
    [[nodiscard]] const std::string& music() const { return m_music; }

    /// Plays front-end sound cue `cue` (an entry of the audio manager's table, docs/research/frontend.md#audio-cues).
    void playCue(int cue);
    /// The cues asked for, in order.
    [[nodiscard]] const std::vector<int>& cues() const { return m_cues; }

    /// Plays the movie `name` (`LOGO`, `L1_IN`): skipped.
    void playMovie(std::string_view name);
    /// The movies asked for, in order.
    [[nodiscard]] const std::vector<std::string>& movies() const { return m_movies; }

    /// Calls the Lua function `function` (a dotted name such as `Menu.playMovie`) with numeric `args`: skipped.
    void callScript(std::string_view function, std::span<const double> args = {});
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
};

} // namespace coney
