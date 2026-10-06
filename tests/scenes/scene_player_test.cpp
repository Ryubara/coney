// SPDX-License-Identifier: GPL-3.0-or-later
// The scene player plays synthetic scenes as docs/research/scenes.md#behaviour says: loading into slots with a
// callback, the start sequence, the tracks and the roles' clips, segment streaming, the skip and the end.

#include <cmath>
#include <cstdint>
#include <format>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "scenes/letterbox.h"
#include "scenes/scene_player.h"
#include "support/scene_fixtures.h"

using Catch::Approx;
namespace scenes = coney::scenes;
namespace test = coney::test;

namespace {

// A host that writes down what the scene asks of it.
class RecordingHost final : public scenes::SceneHost {
  public:
    std::vector<std::string> calls;
    std::map<double, scenes::RoleFrame> frames;
    std::optional<scenes::SceneLens> lens;
    std::map<double, std::optional<scenes::ScenePose>> released;
    bool ready = true;

    void humanJoin(double human, std::uint32_t scene, std::size_t role, const scenes::ScenePose& /*start*/,
                   int gait) override {
        calls.push_back(std::format("join {} {} {} {}", human, scene, role, gait));
    }
    bool humanReady(double /*human*/) override { return ready; }
    void humanEnterScene(double human, std::size_t role) override {
        calls.push_back(std::format("enter {} {}", human, role));
    }
    void humanPose(double human, const scenes::RoleFrame& frame) override { frames[human] = frame; }
    void humanExitScene(double human) override { calls.push_back(std::format("exit {}", human)); }
    void humanRelease(double human, const std::optional<scenes::ScenePose>& endPose) override {
        released[human] = endPose;
        calls.push_back(std::format("release {}", human));
    }
    void suspendBrains(bool suspended) override { calls.push_back(std::format("brains {}", suspended)); }
    void objectMessage(double object, int message) override {
        calls.push_back(std::format("message {} {:#x}", object, message));
    }
    void objectRelease(double object) override { calls.push_back(std::format("object release {}", object)); }
    void cameraBegin(const scenes::ScenePose& /*pose*/, const scenes::SceneLens& l) override {
        calls.push_back(std::format("camera begin {}", l.fieldOfView));
    }
    void cameraPose(const scenes::ScenePose& /*pose*/, const scenes::SceneLens& l) override { lens = l; }
    void cameraEnd(float blend) override { calls.push_back(std::format("camera end {}", blend)); }
    void screenEffect(scenes::ScreenEffect type, float seconds) override {
        calls.push_back(std::format("screen {} {}", static_cast<int>(type), seconds));
    }
    void caption(std::string_view scene, int command) override {
        calls.push_back(std::format("caption {} {}", scene, command));
    }
    void soundtrackPrepare(std::uint32_t hash) override { calls.push_back(std::format("soundtrack {:#x}", hash)); }
    void soundtrackStart() override { calls.emplace_back("soundtrack start"); }

    // Whether a call equal to `call` was made.
    [[nodiscard]] bool made(std::string_view call) const { return std::ranges::find(calls, call) != calls.end(); }
    // The index of the first call equal to `call`; calls.size() when none.
    [[nodiscard]] std::size_t indexOf(std::string_view call) const {
        return static_cast<std::size_t>(std::ranges::find(calls, call) - calls.begin());
    }
};

// A scene system over a list of `tst_c1`, its segment and a long one-part scene `tst_long` (3 s), recording the Lua
// calls it makes.
struct Harness {
    scenes::SceneList list{std::vector<scenes::SceneListEntry>{
        {0, 0, "tst_first"}, {1, 0, "tst_c1"}, {2, 0, "tst_c1aa"}, {3, 0, "tst_long"}}};
    std::map<std::string, std::vector<std::byte>, std::less<>> files;
    std::vector<std::string> lua;
    RecordingHost host;
    scenes::SceneSystem system;
    std::uint64_t nowMs = 0;

    Harness()
        : system(
              list,
              [this](std::string_view name) -> std::expected<std::vector<std::byte>, coney::Error> {
                  const auto found = files.find(name);
                  if (found == files.end()) {
                      return coney::fail(coney::ErrorCode::NotFound, std::string(name));
                  }
                  return found->second;
              },
              [this](std::string_view function, std::span<const double> args) {
                  lua.push_back(std::format("{}({})", function, args.empty() ? -1.0 : args[0]));
              }) {
        files["tst_c1"] = test::sceneHeaderRecord(test::twoPartSpec()).data();
        files["tst_c1aa"] = test::sceneSegmentRecord("tst_c1aa", "", test::segmentPart()).data();
        test::SceneSpec longSpec = test::twoPartSpec();
        longSpec.name = "tst_long";
        longSpec.firstSegment.clear();
        longSpec.frames = 90;
        for (test::ClipSpec& clip : longSpec.part.clips) {
            clip.duration = 3.0F;
        }
        files["tst_long"] = test::sceneHeaderRecord(longSpec).data();
        system.setHost(&host);
    }

    // One update, 1/30 s on, with `buttons` held.
    void step(std::uint16_t buttons = 0) {
        nowMs += 33;
        system.update(nowMs, buttons);
    }
    // A cinematic request as global.lua's gPlayCutScene makes it for level99.
    static scenes::PlayRequest cinematic() {
        return scenes::PlayRequest{.kind = scenes::PlayKind::Cinematic,
                                   .onEnd = "PreCashTheWorld",
                                   .cinematic = true,
                                   .skippable = true,
                                   .freeze = false,
                                   .blendCam = -1.0F};
    }
    // A fixed scene that does not loop or freeze.
    static scenes::PlayRequest fixedScene() { return scenes::PlayRequest{.kind = scenes::PlayKind::Fixed}; }
};

} // namespace

TEST_CASE("a preloaded scene calls its callback once it arrives; a second preload only counts a user", "[scenes]") {
    Harness h;
    CHECK(h.system.preload("c1", "gPlayCutScene") == 1);
    CHECK(h.system.state(1) == scenes::SceneState::Loading);
    CHECK_FALSE(h.system.isPreloaded("tst_c1"));
    h.step();
    CHECK(h.lua == std::vector<std::string>{"gPlayCutScene(1)"});
    CHECK(h.system.state(1) == scenes::SceneState::Loaded);
    CHECK(h.system.isPreloaded("tst_c1"));
    CHECK(h.system.length(1) == 1.0F);
    CHECK(h.system.preload("tst_c1", "again") == 1);
    h.step();
    CHECK(h.lua.size() == 1);
    CHECK(h.system.cache().find(1)->users == 1);
    // An unknown name is scene 0, which this list holds but the files do not: it fails to load and frees its slot.
    CHECK(h.system.preload("nothing", "cb") == 0);
    h.step();
    CHECK(h.system.state(0) == scenes::SceneState::Empty);
    h.system.unload(1);
    CHECK(h.system.state(1) == scenes::SceneState::Empty);
}

TEST_CASE("a cinematic starts, plays its parts, streams its segment and ends with its end function", "[scenes]") {
    Harness h;
    h.system.preload("tst_c1", "");
    h.step();
    REQUIRE(h.system.joinHuman(7.0, 1, 0, 2));
    CHECK_FALSE(h.system.joinHuman(8.0, 1, 5, 2)); // no role 5
    h.system.addObject(1, 9.0, 0);
    REQUIRE(h.system.play(1, Harness::cinematic()));
    CHECK_FALSE(h.system.play(1, Harness::cinematic())); // already playing
    CHECK(h.system.state(1) == scenes::SceneState::Starting);
    CHECK_FALSE(h.system.done(1));

    // The start: the blur pulse ended, the scene state stepped 0 -> 1 -> 2 -> 3, then the start itself.
    h.step();
    CHECK(h.host.made("screen 5 0"));
    CHECK(h.system.cinematicActive());
    h.step();
    h.step();
    CHECK(h.system.state(1) == scenes::SceneState::Starting);
    h.step();
    CHECK(h.system.state(1) == scenes::SceneState::Playing);
    CHECK(h.host.made("camera begin 60"));
    CHECK(h.host.made("enter 7 0"));
    CHECK(h.host.made("screen 2 1.5"));
    CHECK(h.host.made("screen 0 0.5")); // the camera track's fade in at frame 0
    CHECK_FALSE(h.host.made("brains true"));
    // Frame 0: role 0 at its marks.
    const scenes::RoleFrame& first = h.host.frames.at(7.0);
    CHECK(first.feet.x == 10.0F);
    CHECK(first.feet.y == 20.0F);
    CHECK(first.heading == Approx(0.5F));
    CHECK(h.system.cache().segmentsRead() == 0);

    // A second of updates: the role walks forward at 1 m/s along its heading; the camera's events fire.
    for (int i = 0; i < 29; ++i) {
        h.step();
    }
    CHECK(h.system.cache().segmentsRead() == 1); // the first segment, asked for once every runner is on part 0
    const scenes::RoleFrame& walked = h.host.frames.at(7.0);
    CHECK(walked.feet.x == Approx(10.0F - std::sin(0.5F) * 29.0F / 30.0F).margin(0.01));
    CHECK(walked.feet.y == Approx(20.0F + std::cos(0.5F) * 29.0F / 30.0F).margin(0.01));
    CHECK(h.host.made("caption tst_c1 0"));
    CHECK_FALSE(h.host.made("screen 1 0.25"));
    CHECK(h.host.made("message 9 0x12"));
    CHECK(h.host.made("soundtrack 0x1234")); // prepared as the scene loaded
    CHECK(h.host.made("soundtrack start"));  // the camera track's event 13
    REQUIRE(h.host.lens.has_value());
    CHECK(h.host.lens.transform([](const scenes::SceneLens& l) { return l.fieldOfView; }) == 40.0F);

    // Scene frame 40, in the segment: the header clip's event puts the role at (100, 100).
    for (int i = 0; i < 11; ++i) {
        h.step();
    }
    CHECK(h.system.frame(1) == Approx(40.0F));
    CHECK(h.host.made("screen 1 0.25")); // the header camera track's event of scene frame 40
    CHECK(h.host.frames.at(7.0).feet.x == Approx(100.0F).margin(0.05));

    // Frame 45 the clip ends: the human leaves, then the end gives everything back and calls the end function.
    for (int i = 0; i < 5; ++i) {
        h.step();
    }
    CHECK(h.host.made("exit 7"));
    CHECK(h.system.state(1) == scenes::SceneState::Ending);
    h.step();
    CHECK(h.system.state(1) == scenes::SceneState::Ended);
    CHECK(h.system.done(1));
    CHECK(h.host.made("release 7"));
    CHECK_FALSE(h.host.released.at(7.0).has_value()); // played out: left where the clip put it
    CHECK(h.host.made("object release 9"));
    CHECK(h.host.made("camera end -1"));
    CHECK(h.host.made("screen 3 1.5"));
    CHECK(h.lua == std::vector<std::string>{"PreCashTheWorld(1)"});
    CHECK_FALSE(h.system.cinematicActive());
    // The next update frees the task and unloads the slot.
    h.step();
    CHECK(h.system.state(1) == scenes::SceneState::Empty);
    CHECK(h.system.stats().started == 1);
    CHECK(h.system.stats().ended == 1);
    CHECK(h.system.stats().skipped == 0);
}

TEST_CASE("cross skips a skippable scene only after 2 s; the humans go to their end poses", "[scenes]") {
    Harness h;
    h.system.preload("tst_long", "");
    h.step();
    REQUIRE(h.system.joinHuman(7.0, 3, 0, 2));
    REQUIRE(h.system.play(3, Harness::cinematic()));
    for (int i = 0; i < 30; ++i) {
        h.step(scenes::kSkipCross);
    }
    CHECK(h.system.state(3) == scenes::SceneState::Playing);
    for (int i = 0; i < 31; ++i) {
        h.step();
    }
    h.step(scenes::kSkipCross);
    CHECK(h.system.state(3) == scenes::SceneState::Ending);
    CHECK(h.host.made("screen 1 0"));
    CHECK(h.host.made("caption tst_long 4"));
    CHECK_FALSE(h.system.chainSkip());
    h.step();
    CHECK(h.system.state(3) == scenes::SceneState::Ended);
    REQUIRE(h.host.released.at(7.0).has_value());
    CHECK(h.host.released.at(7.0).transform([](const scenes::ScenePose& p) { return p.position; }) ==
          coney::anim::Vec3{12.0F, 21.0F, 0.0F});
    CHECK(h.system.stats().skipped == 1);
    CHECK(h.lua == std::vector<std::string>{"PreCashTheWorld(3)"});
}

TEST_CASE("START skips and sets the chain skip; a stop ends a starting scene; freeze suspends the brains", "[scenes]") {
    Harness h;
    h.system.preload("tst_long", "");
    h.step();
    REQUIRE(h.system.joinHuman(7.0, 3, 0, 2));
    scenes::PlayRequest request = Harness::cinematic();
    request.freeze = true;
    REQUIRE(h.system.play(3, request));
    for (int i = 0; i < 70; ++i) {
        h.step();
    }
    CHECK(h.host.made("brains true"));
    h.step(scenes::kSkipStart);
    CHECK(h.system.chainSkip());
    h.step();
    CHECK(h.host.made("brains false"));
    h.step();

    // A scene stopped while it waits for its humans ends without starting.
    h.host.ready = false;
    h.system.preload("tst_long", "");
    h.step();
    REQUIRE(h.system.joinHuman(7.0, 3, 0, 2));
    REQUIRE(h.system.play(3, Harness::fixedScene()));
    h.step();
    CHECK(h.system.state(3) == scenes::SceneState::Starting);
    h.system.stop(3, false);
    h.step();
    CHECK(h.system.state(3) == scenes::SceneState::Ended);
}

TEST_CASE("a looping fixed scene keeps playing until it is stopped", "[scenes]") {
    Harness h;
    h.system.preload("tst_c1", "");
    h.step();
    scenes::PlayRequest request{.kind = scenes::PlayKind::Fixed, .looping = true};
    REQUIRE(h.system.play(1, request));
    for (int i = 0; i < 200; ++i) {
        h.step();
    }
    CHECK(h.system.state(1) == scenes::SceneState::Playing);
    CHECK(h.system.cache().segmentsRead() == 1); // the one segment stays in its buffer from pass to pass
    h.system.stop(1, false);                     // only stops the looping
    for (int i = 0; i < 60; ++i) {
        h.step();
    }
    CHECK(h.system.state(1) == scenes::SceneState::Empty);
    CHECK(h.system.stats().ended == 1);
}

TEST_CASE("a full set of slots evicts the least recently requested idle scene", "[scenes]") {
    std::vector<scenes::SceneListEntry> entries;
    entries.reserve(13);
    for (std::uint32_t id = 0; id < 13; ++id) {
        entries.push_back({id, 0, std::format("s{:02}", id)});
    }
    const scenes::SceneList list(std::move(entries));
    std::vector<std::byte> record = test::sceneHeaderRecord(test::SceneSpec{}).data(); // copied by each read
    scenes::SceneCache cache(list, [&record](std::string_view) { return record; });
    for (std::uint32_t id = 0; id < 12; ++id) {
        REQUIRE(cache.request(id, "", 100 + id) != nullptr);
    }
    cache.service();
    cache.request(0, "", 500); // scene 0 is used again: scene 1 is now the oldest
    REQUIRE(cache.request(12, "", 600) != nullptr);
    CHECK(cache.find(1) == nullptr);
    CHECK(cache.find(0) != nullptr);
    CHECK(cache.find(12) != nullptr);
}

TEST_CASE("the letterbox closes and opens linearly over its time", "[scenes]") {
    scenes::Letterbox bars;
    CHECK(bars.amount(0) == 0.0F);
    bars.start(true, 1.5F, 1000);
    CHECK(bars.amount(1000) == 0.0F);
    CHECK(bars.amount(1750) == Approx(0.5F));
    CHECK(bars.barHeight(2500) == scenes::Letterbox::kBarHeight);
    bars.start(false, 1.5F, 3000);
    CHECK(bars.amount(3750) == Approx(0.5F));
    CHECK(bars.amount(9000) == 0.0F);
    bars.start(true, 0.0F, 9000); // a chained scene's bars close at once
    CHECK(bars.amount(9000) == 1.0F);
}
