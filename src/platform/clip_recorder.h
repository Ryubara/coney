// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/clip_recording.h"
#include "core/error.h"

namespace coney::platform {

/// The git checkout (a folder holding `.git`: a main checkout or a worktree) that `path` lies in, or nothing.
[[nodiscard]] std::optional<std::filesystem::path> gitCheckoutAround(const std::filesystem::path& path);

/// Checks where a clip may be written, on the file system: its folder exists and lies in no git checkout (clips never
/// enter the repository). clips::checkClipPath() checks the name and the `clips` folder first.
[[nodiscard]] std::expected<void, Error> checkClipLocation(const std::string& path);

/// What a finished clip holds, for the run's log: counts and a hash of the frames, never their content.
struct ClipSummary {
    std::string path;
    int frames = 0;
    clips::ClipSize source;
    clips::ClipSize size;
    bool sound = false;
    std::uint64_t bytes = 0;
    std::uint64_t framesHash = 0; ///< FNV-1a of every frame's pixels, in order: equal runs give equal hashes.
};

/// `coney --record`: Coney's own debug tooling (the original has nothing like it) that writes a short H.264 clip of
/// Coney's frames for an issue or a pull request (docs/guides/testing.md#clips).
///
/// It only ever sees what it is handed: the frames RenderEngine reads back from Coney's own frame buffer before the
/// debug menus are drawn (RenderEngine::setFrameTap()) and the steps' mixed sound (AudioOutput::setStepTap()). It
/// never reads the screen. The frames go to an external FFmpeg (`CONEY_FFMPEG`, else `ffmpeg` on PATH) through its
/// standard input, its only input; the sound is kept in memory (ten seconds are under 2 MB) and muxed in a second
/// pass. Both passes write no metadata (clips::videoEncodeArguments(), clips::muxArguments()); their part files sit
/// beside the clip and are deleted.
class ClipRecorder {
  public:
    /// A recorder for `seconds` (clamped to 1 to clips::kMaxClipSeconds) into `path`, which must pass
    /// clips::checkClipPath() and checkClipLocation(). `sound`: whether the steps' sound will be handed over. Nothing
    /// starts until start() and the first frame.
    [[nodiscard]] static std::expected<std::unique_ptr<ClipRecorder>, Error> create(const std::string& path,
                                                                                    int seconds, bool sound);
    ~ClipRecorder();
    ClipRecorder(const ClipRecorder&) = delete;
    ClipRecorder& operator=(const ClipRecorder&) = delete;

    /// Starts taking frames from the next addFrame() on.
    void start() { m_started = true; }
    /// Whether it takes frames now: started, not full, no failure.
    [[nodiscard]] bool recording() const { return m_started && !m_frames.full() && !m_failure && !m_finished; }
    /// Whether the clip has all its frames (finish() is then due).
    [[nodiscard]] bool full() const { return m_frames.full() || m_failure.has_value(); }

    /// One rendered frame: `rgba`, `size.width` x `size.height` pixels from the top left, 4 bytes each. The first one
    /// starts FFmpeg at its size; a frame of another size later is a failure (finish() reports it).
    void addFrame(std::span<const std::uint8_t> rgba, clips::ClipSize size);
    /// One step's mixed sound (interleaved 16-bit stereo), kept when the frame of that step was taken.
    void addSound(std::span<const std::int16_t> samples);

    /// Ends the clip: closes the video pass, muxes the sound and deletes the part files. Called once full(), or when
    /// the run ends first (a shorter clip). Any failure removes what was written.
    [[nodiscard]] std::expected<ClipSummary, Error> finish();

  private:
    ClipRecorder(std::string path, int seconds, bool sound, std::string ffmpeg);
    // Starts the video pass for frames of `size`; false (and m_failure set) when FFmpeg cannot start.
    bool startVideo(clips::ClipSize size);
    // Writes all of `bytes` to FFmpeg's standard input, which SDL makes non-blocking: false when FFmpeg has gone.
    bool writeToVideo(std::span<const std::uint8_t> bytes);
    // Waits for the process `process` (an SDL_Process*) and destroys it; true when it ended with exit code 0.
    static bool waitAndDestroy(void* process);
    // Removes the part files and, after a failure, the clip.
    void removeParts(bool clipToo) const;

    std::string m_path;
    std::string m_ffmpeg;
    int m_seconds;
    bool m_sound;
    clips::ClipFrames m_frames;
    bool m_started = false;
    bool m_finished = false;
    bool m_soundDue = false; // the last addFrame() took its frame, so that step's sound belongs to the clip
    void* m_video = nullptr; // SDL_Process* of the video pass
    clips::ClipSize m_source;
    std::vector<std::int16_t> m_samples;
    std::uint64_t m_hash = 0xcbf29ce484222325ULL; // FNV-1a's offset basis
    std::optional<Error> m_failure;
};

} // namespace coney::platform
