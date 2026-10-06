// SPDX-License-Identifier: GPL-3.0-or-later
// The movie player as a game mode (docs/research/movies.md#player): its clock, its end, skipping, its sound and its
// queue, over a fake decoder. Nothing from the disc.
#include "movies/movie_mode.h"

#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "audio/mixer.h"
#include "core/pad.h"
#include "core/pads.h"
#include "gamemodes/game_mode_stack.h"
#include "support/recording_device.h"

namespace {

using coney::movies::MovieDecoder;
using coney::movies::MovieInfo;
using coney::movies::MovieMode;

// A movie of `frames` frames at `numerator`/`denominator` a second whose frame k is filled with the byte k; with
// sound, 1,600 stereo frames of a constant value come with each picture.
class FakeDecoder final : public MovieDecoder {
  public:
    FakeDecoder(std::uint32_t frames, std::uint32_t numerator, std::uint32_t denominator, bool sound) {
        m_info.width = 4;
        m_info.height = 2;
        m_info.rateNumerator = numerator;
        m_info.rateDenominator = denominator;
        m_info.frameCount = frames;
        m_info.hasAudio = sound;
        m_info.sampleRate = sound ? 48'000 : 0;
        m_info.channels = sound ? 2 : 0;
    }
    [[nodiscard]] const MovieInfo& info() const override { return m_info; }
    std::expected<bool, coney::Error> decodeFrame(std::span<std::uint8_t> rgba) override {
        if (m_next >= m_info.frameCount) {
            return false;
        }
        for (std::uint8_t& byte : rgba) {
            byte = static_cast<std::uint8_t>(m_next);
        }
        ++m_next;
        if (m_info.hasAudio) {
            m_pcm.insert(m_pcm.end(), std::size_t{1600} * 2, 8000);
        }
        return true;
    }
    std::size_t takeAudio(std::vector<std::int16_t>& out) override {
        const std::size_t count = m_pcm.size();
        out.insert(out.end(), m_pcm.begin(), m_pcm.end());
        m_pcm.clear();
        return count;
    }

  private:
    MovieInfo m_info;
    std::uint32_t m_next = 0;
    std::vector<std::int16_t> m_pcm;
};

// A screen that keeps the last frame it was given.
class FakeScreen final : public coney::movies::MovieScreen {
  public:
    const coney::graphics::Texture* upload(std::span<const std::uint8_t> rgba, int width, int height) override {
        last.assign(rgba.begin(), rgba.end());
        ++uploads;
        return width == 4 && height == 2 ? &texture : nullptr;
    }
    std::vector<std::uint8_t> last;
    int uploads = 0;
    coney::test::FakeTexture texture{4, 2};
};

// Pad samples from a map of step to the buttons held on port 1.
class HeldButtons final : public coney::InputSource {
  public:
    explicit HeldButtons(std::map<std::uint64_t, std::uint16_t> held) : m_held(std::move(held)) {}
    [[nodiscard]] coney::PortSamples sample(std::uint64_t frame) override {
        coney::PortSamples samples{};
        samples[0].connected = true;
        if (auto it = m_held.find(frame); it != m_held.end()) {
            samples[0].buttons = it->second;
        }
        return samples;
    }

  private:
    std::map<std::uint64_t, std::uint16_t> m_held;
};

// A mode that only counts its updates: what waits beneath the movie player.
class WaitingMode final : public coney::GameMode {
  public:
    [[nodiscard]] std::uint32_t id() const override { return 0x1ff; }
    coney::ModeResult update(coney::GameModeStack& /*stack*/, const coney::FrameTime& /*frame*/) override {
        ++updates;
        return coney::ModeResult::Stay;
    }
    int updates = 0;
};

// A stack, a device, a fake screen and a movie player over fake movies (`LOGO` 10 frames at 29.97 with sound, the
// others 20 at 30 without), stepped with the pads sampled each step.
struct Harness {
    explicit Harness(std::map<std::uint64_t, std::uint16_t> held = {}, bool skipAll = false) : input(std::move(held)) {
        stack.setInput(&input);
        stack.push(below);
        coney::movies::MovieSettings settings;
        settings.skipAll = skipAll;
        player = std::make_unique<MovieMode>(
            stack, device,
            [this](std::string_view name) -> std::expected<std::unique_ptr<MovieDecoder>, coney::Error> {
                opened.emplace_back(name);
                if (name == "MISSING") {
                    return coney::fail(coney::ErrorCode::NotFound, "no PSS/MISSING.BIK");
                }
                if (name == "LOGO") {
                    return std::make_unique<FakeDecoder>(10, 2997, 100, true);
                }
                return std::make_unique<FakeDecoder>(20, 30, 1, false);
            },
            &screen, &mixer, settings, [this](std::string_view line) { log.emplace_back(line); });
    }
    // Runs one step and one render, as the test-mode loop does.
    void step() {
        stack.samplePads(steps);
        stack.step(coney::FrameTime{steps, 1.0 / 30.0, 0, 0});
        stack.render(coney::RenderTime{});
        ++steps;
    }
    // Steps until the player is off the stack (at most `limit` steps); returns the steps taken.
    int runToEnd(int limit = 1000) {
        int taken = 0;
        while (stack.top() != &below && taken < limit) {
            step();
            ++taken;
        }
        return taken;
    }

    HeldButtons input;
    coney::GameModeStack stack;
    WaitingMode below;
    coney::test::RecordingDevice device;
    FakeScreen screen;
    coney::audio::Mixer mixer;
    std::unique_ptr<MovieMode> player;
    std::vector<std::string> opened;
    std::vector<std::string> log;
    std::uint64_t steps = 0;
};

} // namespace

TEST_CASE("movie player: pushes itself over the caller, plays to the last frame, which it never shows", "[movies]") {
    Harness h;
    h.player->playMovie("PLOGO");
    CHECK(h.stack.top() == h.player.get());
    // 20 frames at 30 a second: frames 0 to 18 shown, one a step; the step frame 19 is due ends it.
    CHECK(h.runToEnd() == 20);
    CHECK(h.player->counts().framesShown == 19);
    CHECK(h.player->counts().framesDecoded == 19);
    CHECK(h.screen.uploads == 19);
    CHECK(h.screen.last == std::vector<std::uint8_t>(4 * 2 * 4, 18));
    CHECK(h.below.updates == 0); // the caller waited
    CHECK(h.log.back() == "movie: PLOGO ended after 19 frames\n");
    // The frame is drawn unscaled, centred on the 640 x 448 screen.
    REQUIRE_FALSE(h.device.draws.empty());
    const coney::graphics::LogicalQuad& quad = h.device.draws.front().quads.front();
    CHECK(quad.x == 318.0F);
    CHECK(quad.y == 223.0F);
    CHECK(quad.width == 4.0F);
    CHECK(quad.height == 2.0F);
    // The step it leaves on draws black: no frame.
    CHECK(h.device.draws.size() == 19);
}

TEST_CASE("movie player: a 29.97 movie follows its own rate on the 30-a-second step", "[movies]") {
    Harness h;
    h.player->playMovie("LOGO");
    // 10 frames at 29.97: frame 9 is due at 9 / 29.97 s, step 10 (10 / 30 s is 9.99 frames in).
    CHECK(h.runToEnd() == 11);
    CHECK(h.player->counts().framesDecoded == 9);
    // Step 9 (9 / 30 s = 8.99 frames) still shows frame 8: one step shows no new frame.
    CHECK(h.player->counts().framesShown == 9);
}

TEST_CASE("movie player: any button skips a skippable movie, never LOGO; the next one starts a step later",
          "[movies]") {
    // Presses during LOGO do nothing.
    Harness logo({{3, coney::pad::kCross}, {6, coney::pad::kUp}, {8, coney::pad::kStart}});
    logo.player->playMovie("LOGO");
    CHECK(logo.runToEnd() == 11);
    CHECK(logo.player->counts().skipped == 0);
    CHECK(logo.log.back() == "movie: LOGO ended after 9 frames\n");

    Harness skips({{12, coney::pad::kUp}, {14, coney::pad::kCircle}});
    skips.player->playMovie("LOGO");
    skips.player->playMovie("PLOGO");
    skips.player->playMovie("L1_IN");
    // LOGO ends at step 10; PLOGO starts at 11 and the d-pad skips it at 12; L1_IN starts at 13, circle skips it at 14.
    CHECK(skips.runToEnd() == 15);
    CHECK(skips.opened == std::vector<std::string>{"LOGO", "PLOGO", "L1_IN"});
    CHECK(skips.player->counts().skipped == 2);
    CHECK(skips.log.back() == "movie: L1_IN skipped after 2 frames\n");
}

TEST_CASE("movie player: a held button skips a movie at once", "[movies]") {
    Harness h({{0, coney::pad::kSquare}, {1, coney::pad::kSquare}});
    h.player->playMovie("PLOGO");
    CHECK(h.runToEnd() == 1);
    CHECK(h.player->counts().skipped == 1);
    CHECK(h.player->counts().framesShown == 1); // the first frame was shown before the pads were read
}

TEST_CASE("movie player: a movie that cannot be opened is skipped; --skip-movies skips every one", "[movies]") {
    Harness h;
    h.player->playMovie("MISSING");
    h.player->playMovie("PLOGO");
    h.runToEnd();
    CHECK(h.player->counts().failed == 1);
    CHECK(h.player->counts().movies == 1);
    CHECK(h.log.front() == "movie: MISSING not played (no PSS/MISSING.BIK)\n");

    Harness skipped({}, true);
    skipped.player->playMovie("PLOGO");
    CHECK(skipped.stack.top() == &skipped.below);
    CHECK(skipped.opened.empty());
}

TEST_CASE("movie player: the sound streams to the mixer at 80 % volume", "[movies]") {
    Harness h;
    h.player->playMovie("LOGO");
    h.step();
    h.step();
    h.step(); // steps 0 and 2 decode a frame; step 1 still shows frame 0
    std::vector<std::int16_t> out(std::size_t{1600} * 2);
    h.mixer.mix(out);
    // 8,000 at 0x6665 / 0x7fff of full volume, on the music bus at its default volume.
    const float expected = 8000.0F * MovieMode::kMovieVolume * h.mixer.busVolume(coney::audio::Bus::Music);
    CHECK(out.back() >= static_cast<std::int16_t>(expected - 2.0F));
    CHECK(out.back() <= static_cast<std::int16_t>(expected + 2.0F));
    CHECK(h.player->counts().samples == std::uint64_t{2} * 1600 * 2);
    CHECK(MovieMode::kMovieVolume > 0.79F);
    CHECK(MovieMode::kMovieVolume < 0.81F);
}

TEST_CASE("movie player: only LOGO cannot be skipped", "[movies]") {
    CHECK_FALSE(coney::movies::movieSkippable("LOGO"));
    CHECK(coney::movies::movieSkippable("PLOGO"));
    CHECK(coney::movies::movieSkippable("L99_IN"));
}
