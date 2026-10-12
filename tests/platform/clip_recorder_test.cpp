// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/clip_recorder.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <catch2/catch_test_macros.hpp>

using coney::platform::checkClipLocation;
using coney::platform::ClipRecorder;
using coney::platform::gitCheckoutAround;

namespace {
// A fresh folder under the system's temporary folder, removed when the test ends.
struct TempFolder {
    std::filesystem::path path;
    explicit TempFolder(const std::string& name)
        : path(std::filesystem::temp_directory_path() / ("coney-clip-test-" + name)) {
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TempFolder() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
    TempFolder(const TempFolder&) = delete;
    TempFolder& operator=(const TempFolder&) = delete;
};
} // namespace

TEST_CASE("a clip folder inside a git checkout is refused", "[clips]") {
    TempFolder root("checkout");
    std::filesystem::create_directories(root.path / "repo" / "scratch" / "task" / "clips");
    // A worktree's .git is a file; a main checkout's is a folder. Either marks a checkout.
    std::ofstream(root.path / "repo" / ".git") << "gitdir: elsewhere\n";
    const std::filesystem::path clip = root.path / "repo" / "scratch" / "task" / "clips" / "a.mp4";
    REQUIRE(gitCheckoutAround(clip).has_value());
    CHECK_FALSE(checkClipLocation(clip.string()).has_value());
    CHECK_FALSE(ClipRecorder::create(clip.string(), 5, false).has_value());
}

TEST_CASE("a clip folder outside any checkout is accepted", "[clips]") {
    TempFolder root("scratch");
    std::filesystem::create_directories(root.path / "task" / "clips");
    const std::filesystem::path clip = root.path / "task" / "clips" / "a.mp4";
    CHECK_FALSE(gitCheckoutAround(clip).has_value());
    CHECK(checkClipLocation(clip.string()).has_value());
    // A missing folder is refused rather than made: clips go only where a task's clips folder already is.
    CHECK_FALSE(checkClipLocation((root.path / "other" / "clips" / "a.mp4").string()).has_value());
}

TEST_CASE("the repository itself never takes a clip", "[clips]") {
    // This file lives in the checkout; so would any clips/ folder beside it.
    const std::filesystem::path here{__FILE__};
    CHECK(gitCheckoutAround(here.parent_path() / "clips" / "a.mp4").has_value());
}

// With CONEY_FFMPEG naming an FFmpeg, three synthetic frames make a clip; without it the test skips (CI has none).
TEST_CASE("a recorder writes a clip through FFmpeg", "[clips]") {
    if (SDL_getenv("CONEY_FFMPEG") == nullptr) {
        SKIP("CONEY_FFMPEG is not set");
    }
    TempFolder root("ffmpeg");
    std::filesystem::create_directories(root.path / "task" / "clips");
    const std::string clip = (root.path / "task" / "clips" / "synthetic.mp4").string();
    auto recorder = ClipRecorder::create(clip, 1, true);
    REQUIRE(recorder.has_value());
    (*recorder)->start();
    const coney::clips::ClipSize size{64, 48};
    std::vector<std::uint8_t> frame(static_cast<std::size_t>(size.width) * size.height * 4, 0);
    const std::vector<std::int16_t> sound(std::size_t{1600} * 2, 100);
    for (int i = 0; i < 40; ++i) {
        std::fill(frame.begin(), frame.end(), static_cast<std::uint8_t>(i * 6));
        (*recorder)->addFrame(frame, size);
        (*recorder)->addSound(sound);
    }
    CHECK((*recorder)->full());
    const auto summary = (*recorder)->finish();
    REQUIRE(summary.has_value());
    CHECK(summary->frames == 30);
    CHECK(summary->sound);
    CHECK(summary->size == coney::clips::ClipSize{960, 720});
    CHECK(summary->bytes > 0);
    CHECK(std::filesystem::exists(clip));
    CHECK_FALSE(std::filesystem::exists(coney::clips::partPath(clip, ".video.part.mp4")));
    CHECK_FALSE(std::filesystem::exists(coney::clips::partPath(clip, ".audio.part.raw")));
}
