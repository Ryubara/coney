// SPDX-License-Identifier: GPL-3.0-or-later
// The trimmed FFmpeg the movies decode with (docs/guides/building.md#ffmpeg).
#include "platform/ffmpeg_info.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("the linked FFmpeg is the LGPL build with the Bink components", "[ffmpeg]") {
    const coney::platform::FfmpegInfo info = coney::platform::ffmpegInfo();
    CHECK(info.version == "9.0.2");
    CHECK(info.license == "LGPL version 2.1 or later");
    CHECK(info.binkDemuxer);
    CHECK(info.binkVideo);
    CHECK(info.binkAudio);
}
