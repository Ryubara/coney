// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/clip_recording.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace coney::clips;

namespace {
// Whether `args` holds `flag` immediately followed by `value`.
bool hasPair(const std::vector<std::string>& args, std::string_view flag, std::string_view value) {
    for (std::size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == flag && args[i + 1] == value) {
            return true;
        }
    }
    return false;
}

// Whether `args` holds `text` as one argument.
bool has(const std::vector<std::string>& args, std::string_view text) {
    return std::ranges::find(args, text) != args.end();
}

// The inputs (`-i` values) of an FFmpeg command.
std::vector<std::string> inputs(const std::vector<std::string>& args) {
    std::vector<std::string> found;
    for (std::size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == "-i") {
            found.push_back(args[i + 1]);
        }
    }
    return found;
}
} // namespace

TEST_CASE("a clip is at most ten seconds at thirty frames a second", "[clips]") {
    CHECK(kMaxClipSeconds == 10);
    CHECK(kClipFps == 30);
    ClipFrames frames(5);
    CHECK(frames.limit() == 150);
    int taken = 0;
    for (int i = 0; i < 400; ++i) {
        taken += frames.take() ? 1 : 0;
    }
    CHECK(taken == 150);
    CHECK(frames.full());
    CHECK(frames.taken() == 150);
}

TEST_CASE("the frame count is clamped to the cap", "[clips]") {
    CHECK(ClipFrames(99).limit() == kMaxClipSeconds * kClipFps);
    CHECK(ClipFrames(0).limit() == kClipFps);
}

TEST_CASE("the clip is 720 lines high at the source's shape, in even sizes", "[clips]") {
    CHECK(clipOutputSize({640, 448 * 640 / 448}) == ClipSize{720, 720});
    CHECK(clipOutputSize({1280, 960}) == ClipSize{960, 720});
    CHECK(clipOutputSize({853, 480}) == ClipSize{1280, 720});
    CHECK(clipOutputSize({0, 0}) == ClipSize{960, 720});
}

TEST_CASE("a clip's path is a plain mp4 in a clips folder", "[clips]") {
    CHECK(checkClipPath("C:/work/scratch/task/clips/front-end.mp4").has_value());
    CHECK(checkClipPath("../scratch/task/clips/a_1.b.mp4").has_value());
    CHECK_FALSE(checkClipPath("C:/work/scratch/task/front-end.mp4").has_value());
    CHECK_FALSE(checkClipPath("C:/work/scratch/task/clips/front end.mp4").has_value());
    CHECK_FALSE(checkClipPath("C:/work/scratch/task/clips/front-end.mkv").has_value());
    CHECK_FALSE(checkClipPath("C:/work/scratch/task/clips/.mp4").has_value());
    CHECK_FALSE(checkClipPath("clips/").has_value());
}

TEST_CASE("the video pass reads Coney's frames from standard input only", "[clips]") {
    const std::vector<std::string> args = videoEncodeArguments({"ffmpeg", {1280, 960}, "out.mp4", 10});
    REQUIRE(!args.empty());
    CHECK(args.front() == "ffmpeg");
    CHECK(inputs(args) == std::vector<std::string>{"pipe:0"});
    CHECK(hasPair(args, "-f", "rawvideo"));
    CHECK(hasPair(args, "-pixel_format", "rgba"));
    CHECK(hasPair(args, "-video_size", "1280x960"));
    CHECK(hasPair(args, "-framerate", "30"));
    CHECK(args.back() == "out.mp4");
    // Nothing that could read a screen, a window or a device.
    for (const std::string_view device : {"gdigrab", "ddagrab", "x11grab", "avfoundation", "dshow", "lavfi"}) {
        CHECK_FALSE(has(args, device));
    }
}

TEST_CASE("every pass strips metadata and caps the length", "[clips]") {
    const auto video = videoEncodeArguments({"ffmpeg", {960, 720}, "v.mp4", 7});
    const auto mux = muxArguments("ffmpeg", "v.mp4", "a.raw", "out.mp4", 7);
    for (const auto& args : {video, mux}) {
        CHECK(hasPair(args, "-map_metadata", "-1"));
        CHECK(hasPair(args, "-map_chapters", "-1"));
        CHECK(hasPair(args, "-fflags", "+bitexact"));
        CHECK(hasPair(args, "-movflags", "+faststart"));
        CHECK(hasPair(args, "-t", "7"));
        CHECK_FALSE(has(args, "-metadata"));
    }
    CHECK(hasPair(video, "-c:v", "libx264"));
    CHECK(hasPair(video, "-bsf:v", "filter_units=remove_types=6"));
    CHECK(hasPair(video, "-vf", "scale=960:720:flags=lanczos,format=yuv420p"));
    CHECK(inputs(mux) == std::vector<std::string>{"v.mp4", "a.raw"});
    CHECK(hasPair(mux, "-c:v", "copy"));
    CHECK(hasPair(mux, "-c:a", "aac"));
    CHECK(hasPair(mux, "-f", "s16le"));
    CHECK(hasPair(mux, "-ar", "48000"));
}

TEST_CASE("the passes' part files sit beside the clip", "[clips]") {
    CHECK(partPath("x/clips/a.mp4", ".video.part.mp4") == "x/clips/a.video.part.mp4");
    CHECK(partPath("x/clips/a.mp4", ".audio.part.raw") == "x/clips/a.audio.part.raw");
}
