// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "core/error.h"

namespace coney::movies {

/// What a movie file's header says: its picture, its rate and its sound. The game's movies are all 640 × 448, 30 or
/// 29.97 frames a second, with one 48,000 Hz stereo track or none (docs/research/movies.md#the-movies).
struct MovieInfo {
    int width = 0;                     ///< Pixels.
    int height = 0;                    ///< Pixels.
    std::uint32_t rateNumerator = 30;  ///< Frames a second, as a fraction: `30/1` or `2997/100`.
    std::uint32_t rateDenominator = 1; ///< The rate's denominator.
    std::uint32_t frameCount = 0;      ///< Frames in the file.
    bool hasAudio = false;             ///< Whether the file has a sound track; the rest is 0 without one.
    int sampleRate = 0;                ///< Sound frames a second.
    int channels = 0;                  ///< 1 or 2.

    /// Frames a second as a number.
    [[nodiscard]] double frameRate() const {
        return rateDenominator == 0 ? 0.0 : static_cast<double>(rateNumerator) / rateDenominator;
    }
};

/// A movie being decoded, frame by frame, with its sound alongside. The platform layer implements it with FFmpeg's
/// Bink decoders (src/platform/ffmpeg_movie_decoder.h); game code sees only frames and PCM, never a codec.
class MovieDecoder {
  public:
    virtual ~MovieDecoder() = default;
    MovieDecoder() = default;
    MovieDecoder(const MovieDecoder&) = delete;
    MovieDecoder& operator=(const MovieDecoder&) = delete;
    MovieDecoder(MovieDecoder&&) = delete;
    MovieDecoder& operator=(MovieDecoder&&) = delete;

    /// The file's header.
    [[nodiscard]] virtual const MovieInfo& info() const = 0;

    /// Decodes the next frame. With a non-empty `rgba` (width × height × 4 bytes) the picture is written there as
    /// 8-bit red, green, blue, alpha (alpha 255), row by row from the top; an empty span decodes without converting,
    /// for a run that shows nothing. The sound that came with the frame is kept for takeAudio(). Returns false when
    /// the file has no more frames; fails on a damaged file or a read error.
    virtual std::expected<bool, Error> decodeFrame(std::span<std::uint8_t> rgba) = 0;

    /// Moves the sound decoded so far, as interleaved signed 16-bit frames (info().channels a frame), onto the end of
    /// `out`; returns how many samples it moved.
    virtual std::size_t takeAudio(std::vector<std::int16_t>& out) = 0;
};

/// Opens the movie `name` (`LOGO`, `L99_IN`) from the disc: `PSS/<NAME>.BIK`. Fails with ErrorCode::NotFound when
/// there is no such movie, and as the decoder does for a file it cannot read.
using MovieOpener = std::function<std::expected<std::unique_ptr<MovieDecoder>, Error>(std::string_view name)>;

} // namespace coney::movies
