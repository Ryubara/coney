// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// In-game video clips (`coney --record`): Coney's own debug tooling for issues and pull requests, with no counterpart
// in the original. This is the pure part: the cap, the frame count, the picture size, the clip's path rule and the
// FFmpeg arguments. The process and the frame read-back are platform code (src/platform/clip_recorder.h). How the
// rules are kept: docs/guides/testing.md#clips.

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.h"

namespace coney::clips {

/// A clip's frame rate: one frame per fixed 1/30 s step.
inline constexpr int kClipFps = 30;
/// The longest clip, in seconds; `--record-seconds` refuses more. Equal to `workspace.clips.max_seconds` in
/// `.github/conventions.yml` (python/tests/test_clips.py checks it).
inline constexpr int kMaxClipSeconds = 10;
/// A clip's height in pixels; its width follows the source's shape (960 for the usual 4:3).
inline constexpr int kClipHeight = 720;
/// A clip bigger than this is reported: GitHub takes it, but a reader waits for it.
inline constexpr std::uint64_t kClipWarnBytes = 10'000'000;
/// The folder a clip must be written into (`<scratch>/<task>/clips/`).
inline constexpr std::string_view kClipFolder = "clips";
/// The sound the mux pass reads: Coney's mixer output, 16-bit stereo at 48 kHz (audio::kOutputRate).
inline constexpr int kClipAudioRate = 48'000;

/// A picture size in pixels.
struct ClipSize {
    int width = 0;
    int height = 0;
    friend bool operator==(const ClipSize&, const ClipSize&) = default;
};

/// Counts the frames a clip takes: `seconds` (clamped to 1 to kMaxClipSeconds) times kClipFps, then no more.
class ClipFrames {
  public:
    /// A count for a clip of `seconds`.
    explicit ClipFrames(int seconds);
    /// Takes one frame: true while the clip has room for it.
    bool take();
    /// Frames taken so far.
    [[nodiscard]] int taken() const { return m_taken; }
    /// The most it takes.
    [[nodiscard]] int limit() const { return m_limit; }
    /// Whether it has taken all it can.
    [[nodiscard]] bool full() const { return m_taken >= m_limit; }

  private:
    int m_limit;
    int m_taken = 0;
};

/// The clip's size for a source of `source`: kClipHeight lines high at the source's shape, both sides even (H.264 in
/// yuv420p needs them so); 960 x 720 for an empty source.
[[nodiscard]] ClipSize clipOutputSize(ClipSize source);

/// Checks the clip's path as text: a file name of letters, digits, '.', '_' and '-' ending in `.mp4` (no user or
/// machine name), in a folder named kClipFolder. ErrorCode::InvalidArgument otherwise. Whether the folder lies in a
/// git checkout needs the file system: platform::checkClipLocation().
[[nodiscard]] std::expected<void, Error> checkClipPath(std::string_view path);

/// What the video pass encodes.
struct VideoEncode {
    std::string ffmpeg; ///< The FFmpeg executable.
    ClipSize source;    ///< The frames' size as read back.
    std::string output; ///< The video file written.
    int seconds = kMaxClipSeconds;
};

/// FFmpeg's arguments (the executable first) for the video pass: raw RGBA frames of `encode.source` at kClipFps from
/// standard input, its only input, scaled to clipOutputSize() and encoded as H.264 (CRF 26, preset slow, yuv420p)
/// with the encoder's notes removed, no metadata or chapters, bit-exact headers and the index at the front; cut at
/// `encode.seconds`.
[[nodiscard]] std::vector<std::string> videoEncodeArguments(const VideoEncode& encode);

/// FFmpeg's arguments for the mux pass: the video pass's file and the raw sound (16-bit stereo at kClipAudioRate)
/// into `output`, the video copied and the sound encoded as AAC, with the same metadata rules; cut at `seconds`.
[[nodiscard]] std::vector<std::string> muxArguments(const std::string& ffmpeg, const std::string& video,
                                                    const std::string& audio, const std::string& output, int seconds);

/// A pass's part file beside the clip: `clips/a.mp4` with `suffix` `.video.part.mp4` gives `clips/a.video.part.mp4`.
[[nodiscard]] std::string partPath(const std::string& output, std::string_view suffix);

} // namespace coney::clips
