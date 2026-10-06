// SPDX-License-Identifier: GPL-3.0-or-later
// The Audio page driven through the menu model over a fake AudioControls: no device, no sound.
#include <array>
#include <cstddef>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "debug/audio_controls.h"
#include "debug/debug_session.h"
#include "debug/menu_model.h"

using coney::debug::AudioStatus;
using coney::debug::DebugServices;
using coney::debug::DebugSession;
using coney::debug::MenuItem;
using coney::debug::TunableRegistry;

namespace {

// An output that only keeps what the page set.
class FakeAudio final : public coney::debug::AudioControls {
  public:
    [[nodiscard]] std::size_t volumeCount() const override { return volumes.size(); }
    [[nodiscard]] std::string_view volumeName(std::size_t index) const override { return kNames.at(index); }
    [[nodiscard]] float volume(std::size_t index) const override { return volumes.at(index); }
    void setVolume(std::size_t index, float value) override { volumes.at(index) = value; }
    [[nodiscard]] bool testTone() const override { return tone; }
    void setTestTone(bool on) override { tone = on; }
    [[nodiscard]] AudioStatus status() const override {
        return AudioStatus{.device = "fake", .voicesPlaying = tone ? 1U : 0U, .voiceCount = 48, .sampleRate = 48000};
    }

    static constexpr std::array<std::string_view, 2> kNames{"Master", "Effects"};
    std::array<float, 2> volumes{1.0F, 1.0F};
    bool tone = false;
};

} // namespace

TEST_CASE("the Audio page sets the volumes and the test tone through AudioControls", "[debug]") {
    TunableRegistry tunables;
    FakeAudio audio;
    DebugServices services;
    services.audio = [&audio]() -> coney::debug::AudioControls* { return &audio; };
    DebugSession session(tunables, services, nullptr);
    const auto page = session.model().openPage("Audio");
    REQUIRE(page != nullptr);
    CHECK(page->find("Output")->watch() == "fake, 48000 Hz stereo");
    const MenuItem* effects = page->find("Effects");
    REQUIRE(effects != nullptr);
    effects->setNumber(0.25);
    CHECK(audio.volumes[1] == 0.25F);
    CHECK(effects->getNumber() == 0.25);
    page->find("Test tone")->setBool(true);
    CHECK(audio.tone);
    CHECK(page->find("Voices")->watch() == "1 / 48");
}

TEST_CASE("the Audio page says so when the run has no sound", "[debug]") {
    TunableRegistry tunables;
    DebugSession session(tunables, DebugServices{}, nullptr);
    const auto page = session.model().openPage("Audio");
    REQUIRE(page != nullptr);
    REQUIRE(page->items().size() == 1);
    CHECK(page->items().front().label == "No audio");
}
