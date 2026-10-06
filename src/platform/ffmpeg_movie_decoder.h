// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <expected>
#include <memory>

#include "core/error.h"
#include "fileio/disc.h"
#include "fileio/stream.h"
#include "movies/movie_decoder.h"

namespace coney::platform {

/// Opens a Bink movie read from `file` with FFmpeg's Bink demuxer, video decoder and audio (DCT) decoder
/// (docs/guides/building.md#ffmpeg): the file's first sound track, as the original's `BinkOpen` with flags 0 takes
/// track 0. Frames come out as RGBA converted from Bink's YUV 4:2:0 with the BT.601 studio-range matrix (Coney's
/// choice: the page does not say which matrix Bink's EE conversion uses); the sound as 16-bit PCM. Fails with
/// ErrorCode::Invalid when FFmpeg cannot read the file as a Bink movie with a video track.
///
/// Research: docs/research/movies.md#the-movies
[[nodiscard]] std::expected<std::unique_ptr<movies::MovieDecoder>, Error>
openBinkMovie(std::unique_ptr<io::Stream> file);

/// The game's movie opener over `disc`: movie `name` is `PSS/<NAME>.BIK` (`cdrom0:\PSS\<NAME>.BIK;1`,
/// docs/research/movies.md#player). The disc must outlive the opener.
[[nodiscard]] movies::MovieOpener discMovieOpener(const io::Disc& disc);

} // namespace coney::platform
