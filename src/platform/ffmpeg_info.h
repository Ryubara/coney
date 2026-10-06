// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

namespace coney::platform {

/// What the linked FFmpeg is: the trimmed build of cmake/ffmpeg/CMakeLists.txt that decodes the game's Bink movies
/// (docs/guides/building.md#ffmpeg).
struct FfmpegInfo {
    std::string version;      ///< FFmpeg's release, `9.0.2`.
    std::string license;      ///< libavcodec's licence line: `LGPL version 2.1 or later` for every build Coney makes.
    bool binkDemuxer = false; ///< The Bink demuxer is in.
    bool binkVideo = false;   ///< The Bink video decoder is in.
    bool binkAudio = false;   ///< The Bink audio (DCT) decoder is in.
};

/// Asks the linked FFmpeg what it is and holds.
[[nodiscard]] FfmpegInfo ffmpegInfo();

} // namespace coney::platform
