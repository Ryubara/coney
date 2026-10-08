// SPDX-License-Identifier: GPL-3.0-or-later
// The scene player plays synthetic scenes as docs/research/scenes.md#behaviour says: loading into slots with a
// callback, the start sequence, the tracks and the roles' clips, segment streaming, the skip and the end.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <map>
#include <numbers>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "scenes/letterbox.h"
#include "scenes/scene_cache.h"
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
    std::set<double> inPair; // the humans in a grab or a mount: not free to be taken in

    void humanJoin(double human, std::uint32_t scene, std::size_t role, const scenes::ScenePose& /*start*/,
                   int gait) override {
        calls.push_back(std::format("join {} {} {} {}", human, scene, role, gait));
    }
    bool humanReady(double /*human*/) override { return ready; }
    bool humanFree(double human) override { return !inPair.contains(human); }
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
    void objectPose(double object, const scenes::ScenePose& pose) override { objectPoses[object] = pose; }
    bool widescreen() const override { return sixteenNine; }
    std::map<double, scenes::ScenePose> objectPoses;
    bool sixteenNine = false;
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
    bool soundtrackReady() override { return soundtrackBuffered; }
    void soundtrackStop() override { calls.emplace_back("soundtrack stop"); }
    bool soundtrackBuffered = true;
    void sound(std::uint32_t hash, std::optional<double> /*object*/) override {
        calls.push_back(std::format("sound {:#x}", hash));
    }
    void colouredFade(bool out, std::uint32_t rgb, float seconds) override {
        calls.push_back(std::format("coloured {} {:#x} {}", out, rgb, seconds));
    }
    void rumble(int strength) override { calls.push_back(std::format("rumble {}", strength)); }

    // Whether a call equal to `call` was made.
    [[nodiscard]] bool made(std::string_view call) const { return std::ranges::find(calls, call) != calls.end(); }
    // The index of the first call equal to `call`; calls.size() when none.
    [[nodiscard]] std::size_t indexOf(std::string_view call) const {
        return static_cast<std::size_t>(std::ranges::find(calls, call) - calls.begin());
    }
};

// An object scene of one second, like the front end's Wonder Wheel: one object track and nothing else, with a loop
// point (event 29 at frame 0, restarting from frame 0) when `loopPoint` is set.
test::SceneSpec objectSceneSpec(std::string name, bool loopPoint) {
    test::SceneSpec spec;
    spec.name = std::move(name);
    spec.objects = {test::RoleSpec{"wheel", {5.0F, 5.0F, 0.0F}, 0.0F, {5.0F, 5.0F, 0.0F}, 0.0F}};
    test::TrackSpec track{.duration = 1.0F,
                          .positions = {{0, {0.0F, 0.0F, 0.0F}}, {30, {3.0F, 0.0F, 0.0F}}},
                          .rotations = {},
                          .events = {}};
    if (loopPoint) {
        track.events.push_back(test::SceneEventBytes(0, 29).u16At(4, 0));
    }
    spec.part.objects = {track};
    return spec;
}

// A cinematic of 3 s like l99_c1's introductions: a camera at (0, -5, 2) looking along +y with a 60° lens, and an
// intro card `cleon` parked 10 m underground on its own track, which event 73 at frame 6 holds before the camera for
// 1 s (docs/research/scenes.md#intro-cards).
test::SceneSpec cardSceneSpec() {
    test::SceneSpec spec;
    spec.name = "tst_card";
    spec.frames = 90;
    spec.objects = {test::RoleSpec{"cleon", {0.0F, 0.0F, -10.0F}, 0.0F, {0.0F, 0.0F, -10.0F}, 0.0F}};
    spec.camera = test::RoleSpec{"camera", {0.0F, -5.0F, 2.0F}, 0.0F, {0.0F, -5.0F, 2.0F}, 0.0F};
    spec.part.objects = {
        test::TrackSpec{.duration = 3.0F, .positions = {{0, {0.0F, 0.0F, -10.0F}}}, .rotations = {}, .events = {}}};
    spec.part.camera =
        test::TrackSpec{.duration = 3.0F,
                        .positions = {{0, {0.0F, -5.0F, 2.0F}}},
                        .rotations = {{0, {0, 0, 0}}},
                        .events = {test::SceneEventBytes(0, 26).f32At(8, 60.0F).f32At(12, 0.1F).f32At(16, 100.0F),
                                   test::SceneEventBytes(6, 73).u16At(4, 0).f32At(8, 1.0F)}};
    return spec;
}

// A scene system over a list of `tst_c1`, its segment, a long one-part scene `tst_long` (3 s) and the object scenes
// `tst_wheel` (no loop point) and `tst_loop` (a loop point), recording the Lua calls it makes.
struct Harness {
    scenes::SceneList list{std::vector<scenes::SceneListEntry>{{0, 0, "tst_first"},
                                                               {1, 0, "tst_c1"},
                                                               {2, 0, "tst_c1aa"},
                                                               {3, 0, "tst_long"},
                                                               {4, 0, "tst_wheel"},
                                                               {5, 0, "tst_loop"},
                                                               {6, 0, "tst_calls"},
                                                               {7, 0, "tst_card"}}};
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
        // tst_long with its camera track as long as the scene and three calls of the end function (type 31) on it,
        // at frames 10, 70 and 80 (level80's intro hands control around this way), and between them the events a
        // skip's flush does (28 fade in, 76 rumble, 74 coloured fade, 27 fade out) and drops (13 sound).
        test::SceneSpec callsSpec = longSpec;
        callsSpec.name = "tst_calls";
        callsSpec.part.camera->duration = 3.0F;
        callsSpec.part.camera->events = {test::SceneEventBytes(10, 31),
                                         test::SceneEventBytes(70, 31),
                                         test::SceneEventBytes(72, 28).f32At(8, 0.75F),
                                         test::SceneEventBytes(74, 13).u32At(8, 0x1234),
                                         test::SceneEventBytes(76, 76).u16At(6, 4),
                                         test::SceneEventBytes(78, 74).u32At(4, 0x80ff0000U).f32At(8, 1.0F),
                                         test::SceneEventBytes(80, 31),
                                         test::SceneEventBytes(85, 27).f32At(8, 1.0F)};
        files["tst_calls"] = test::sceneHeaderRecord(callsSpec).data();
        files["tst_card"] = test::sceneHeaderRecord(cardSceneSpec()).data();
        files["tst_wheel"] = test::sceneHeaderRecord(objectSceneSpec("tst_wheel", false)).data();
        files["tst_loop"] = test::sceneHeaderRecord(objectSceneSpec("tst_loop", true)).data();
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

TEST_CASE("a preloaded scene calls its callback once it arrives; a second preload only adds a user", "[scenes]") {
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
    CHECK(h.system.cache().find(1)->users == 2);
    // An unknown name is scene 0, which this list holds but the files do not: it fails to load and frees its slot.
    CHECK(h.system.preload("nothing", "cb") == 0);
    h.step();
    CHECK(h.system.state(0) == scenes::SceneState::Empty);
    // Each unload is one user less; the record stays, idle, until the last one goes.
    h.system.unload(1);
    CHECK(h.system.state(1) == scenes::SceneState::Loaded);
    CHECK(h.system.cache().find(1)->users == 1);
    CHECK(h.system.isPreloaded("tst_c1"));
    h.system.unload(1);
    CHECK(h.system.state(1) == scenes::SceneState::Empty);
    CHECK_FALSE(h.system.isPreloaded("tst_c1"));
}

TEST_CASE("the cache counts users: 1 on arrival, one more per request, the slot freed at 0", "[scenes]") {
    const scenes::SceneList list(std::vector<scenes::SceneListEntry>{{0, 0, "s00"}});
    std::vector<std::byte> record = test::sceneHeaderRecord(test::SceneSpec{}).data(); // copied by each read
    scenes::SceneCache cache(list, [&record](std::string_view) { return record; });
    using Calls = std::vector<std::pair<std::string, std::uint32_t>>;
    REQUIRE(cache.request(0, "first", 1) != nullptr);
    CHECK(cache.find(0)->users == 0); // still loading
    CHECK(cache.service() == Calls{{"first", 0}});
    CHECK(cache.find(0)->users == 1);
    // A request of a scene in any state but empty only adds a user: no callback, and a playing one plays on.
    for (const scenes::SceneState state :
         {scenes::SceneState::Loaded, scenes::SceneState::Playing, scenes::SceneState::Ended}) {
        cache.find(0)->state = state;
        REQUIRE(cache.request(0, "again", 2) != nullptr);
        CHECK(cache.service().empty());
        CHECK(cache.find(0)->state == state);
    }
    CHECK(cache.find(0)->users == 4);
    // An unload of an ended scene with users left: back to Loaded, the record kept.
    cache.unload(0);
    CHECK(cache.find(0)->users == 3);
    CHECK(cache.find(0)->state == scenes::SceneState::Loaded);
    CHECK(cache.find(0)->header != nullptr);
    cache.unload(0);
    cache.unload(0);
    CHECK(cache.find(0) != nullptr);
    cache.unload(0);
    CHECK(cache.find(0) == nullptr);
    // A request while the file is still on its way counts too: two users once it arrives.
    REQUIRE(cache.request(0, "cb", 3) != nullptr);
    REQUIRE(cache.request(0, "other", 4) != nullptr);
    CHECK(cache.service() == Calls{{"cb", 0}});
    CHECK(cache.find(0)->users == 2);
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
    CHECK_FALSE(h.host.made("soundtrack stop")); // played out: the soundtrack runs on to its own end
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
    CHECK(h.host.made("soundtrack stop")); // a skip stops the soundtrack
}

TEST_CASE("a cinematic's start waits until its soundtrack is buffered or its pending preload has run", "[scenes]") {
    Harness h;
    h.system.preload("tst_c1", "");
    h.step();
    h.host.soundtrackBuffered = false;
    REQUIRE(h.system.play(1, Harness::cinematic()));
    for (int i = 0; i < 6; ++i) {
        h.step();
    }
    CHECK(h.system.state(1) == scenes::SceneState::Starting);
    CHECK_FALSE(h.host.made("soundtrack start"));
    h.host.soundtrackBuffered = true;
    h.step();
    h.step();
    CHECK(h.system.state(1) == scenes::SceneState::Playing);
    // A fixed scene does not wait.
    h.host.soundtrackBuffered = false;
    h.system.preload("tst_wheel", "");
    h.step();
    REQUIRE(h.system.play(4, Harness::fixedScene()));
    h.step();
    h.step();
    CHECK(h.system.state(4) == scenes::SceneState::Playing);
}

TEST_CASE("a skip flushes the tracks' remaining events: n pending end calls give n + 1, fades at once", "[scenes]") {
    // docs/research/scenes.md#skipping (SceneTrack_Flush): global.lua's PreCashTheWorld counts NumCallBacks down on
    // each type-31 call and fades back in only once they are spent, so the flush's calls are what bring level80's
    // skipped intro back from black. Fades are flushed at once; sounds are dropped; the camera's pop stops a rumble.
    Harness h;
    h.system.preload("tst_calls", "");
    h.step();
    REQUIRE(h.system.joinHuman(7.0, 6, 0, 2));
    REQUIRE(h.system.play(6, Harness::cinematic()));
    for (int i = 0; i < 62; ++i) {
        h.step();
    }
    REQUIRE(h.system.state(6) == scenes::SceneState::Playing);
    CHECK(h.lua == std::vector<std::string>{"PreCashTheWorld(6)"}); // frame 10's call, played out
    h.host.calls.clear();
    h.step(scenes::kSkipCross);
    h.step();
    CHECK(h.system.state(6) == scenes::SceneState::Ended);
    // Frame 10's call, the two pending ones (70, 80), then the end's own with the scene id.
    CHECK(h.lua == std::vector<std::string>(4, "PreCashTheWorld(6)"));
    // The camera's pending events in track order, at once, before its pop; the pop sets the rumble back to 0.
    const std::size_t fadeIn = h.host.indexOf("screen 0 0");
    const std::size_t rumble = h.host.indexOf("rumble 102");
    const std::size_t coloured = h.host.indexOf("coloured true 0xff0000 0");
    const std::size_t pop = h.host.indexOf("camera end -1");
    CHECK(fadeIn < rumble);
    CHECK(rumble < coloured);
    CHECK(coloured < pop);
    CHECK(h.host.indexOf("rumble 0") > pop);
    CHECK(h.host.indexOf("rumble 0") < h.host.calls.size());
    CHECK_FALSE(h.host.made("screen 0 0.75"));
    CHECK_FALSE(h.host.made("screen 1 1"));
    CHECK_FALSE(h.host.made("sound 0x1234"));
    // Nothing fires twice afterwards.
    for (int i = 0; i < 40; ++i) {
        h.step();
    }
    CHECK(h.lua.size() == 4);
}

TEST_CASE("a skipped looping scene with a loop point plays to the end of its pass", "[scenes]") {
    Harness h;
    scenes::PlayRequest looping{.kind = scenes::PlayKind::Fixed, .looping = true};
    looping.skippable = true;
    h.system.preload("tst_loop", "");
    h.step();
    REQUIRE(h.system.play(5, looping));
    for (int i = 0; i < 75; ++i) { // past the 2 s before a button counts, half way through the third pass
        h.step();
    }
    REQUIRE(h.system.state(5) == scenes::SceneState::Playing);
    h.step(scenes::kSkipCross);
    CHECK(h.host.made("screen 1 0"));
    CHECK(h.system.stats().skipped == 1);
    int updates = 0;
    while (h.system.state(5) == scenes::SceneState::Playing && updates < 100) {
        h.step(scenes::kSkipCross); // held: the skip is not made again
        ++updates;
    }
    CHECK(updates >= 13);
    CHECK(updates <= 16);
    CHECK(h.system.stats().skipped == 1);
}

TEST_CASE("a bound human in a grab at the start is left out: not taken in, not posed, not placed", "[scenes]") {
    // docs/research/scenes.md#humans: the start skips a human that is grabbing, grabbed, mounting or mounted; the
    // scene plays on without it and, not skipped, lets it go where it stands.
    Harness h;
    h.system.preload("tst_long", "");
    h.step();
    REQUIRE(h.system.joinHuman(7.0, 3, 0, 2));
    h.host.inPair.insert(7.0);
    REQUIRE(h.system.play(3, Harness::cinematic()));
    for (int i = 0; i < 20 && h.system.state(3) != scenes::SceneState::Playing; ++i) {
        h.step();
    }
    CHECK(h.system.state(3) == scenes::SceneState::Playing);
    CHECK_FALSE(h.host.made("enter 7 0"));
    for (int i = 0; i < 120 && h.system.state(3) != scenes::SceneState::Ended; ++i) {
        h.step();
    }
    CHECK(h.system.state(3) == scenes::SceneState::Ended);
    CHECK_FALSE(h.host.frames.contains(7.0));
    CHECK_FALSE(h.host.made("exit 7"));
    REQUIRE(h.host.released.contains(7.0));
    CHECK_FALSE(h.host.released.at(7.0).has_value());
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

TEST_CASE("a looping scene without a loop point plays until stopped, then ends at once", "[scenes]") {
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
    // No event 29 on any track or clip: a stop that is not forced ends it mid-pass, like any other scene.
    h.system.stop(1, false);
    CHECK(h.system.state(1) == scenes::SceneState::Ending);
    h.step();
    CHECK(h.system.state(1) == scenes::SceneState::Ended);
    CHECK(h.system.stats().ended == 1);
    h.step(); // freed, its one user gone: the slot is empty
    CHECK(h.system.state(1) == scenes::SceneState::Empty);
}

TEST_CASE("a looping scene with a loop point only stops looping and ends after its pass", "[scenes]") {
    Harness h;
    const scenes::PlayRequest looping{.kind = scenes::PlayKind::Fixed, .looping = true};
    h.system.preload("tst_loop", "");
    h.step();
    REQUIRE(h.system.play(5, looping));
    for (int i = 0; i < 45; ++i) { // the first update starts it; then half way through the second pass
        h.step();
    }
    REQUIRE(h.system.state(5) == scenes::SceneState::Playing);
    h.system.stop(5, false);
    int updates = 0;
    while (h.system.state(5) == scenes::SceneState::Playing && updates < 100) {
        h.step();
        ++updates;
    }
    // The pass runs out (about 15 more updates), not a third one.
    CHECK(updates >= 14);
    CHECK(updates <= 16);

    // A forced stop ends even a scene with a loop point at once.
    h.step();
    h.step();
    REQUIRE(h.system.state(5) == scenes::SceneState::Empty);
    h.system.preload("tst_loop", "");
    h.step();
    REQUIRE(h.system.play(5, looping));
    for (int i = 0; i < 10; ++i) {
        h.step();
    }
    h.system.stop(5, true);
    CHECK(h.system.state(5) == scenes::SceneState::Ending);
}

TEST_CASE("the front end's wheel: stopped, unloaded, preloaded again, it plays afresh from frame 0", "[scenes]") {
    Harness h;
    const scenes::PlayRequest wheel{.kind = scenes::PlayKind::Fixed, .looping = true};
    // startScene: not preloaded, so ScenePreload; the callback plays it looping.
    CHECK_FALSE(h.system.isPreloaded("tst_wheel"));
    h.system.preload("tst_wheel", "startScene");
    h.step();
    CHECK(h.lua == std::vector<std::string>{"startScene(4)"});
    REQUIRE(h.system.play(4, wheel));
    for (int i = 0; i < 21; ++i) {
        h.step();
    }
    REQUIRE(h.system.frame(4).has_value());
    CHECK(h.system.frame(4).value_or(-1.0F) == Approx(20.0F).margin(0.01F));

    // stopScene: no loop point, so the scene ends mid-pass and its slot empties; nothing updates during the movie.
    h.system.stop(4, false);
    h.step();
    h.step();
    CHECK(h.system.state(4) == scenes::SceneState::Empty);

    // startScene again: the slot is empty, so the record loads afresh, the callback comes and it plays from frame 0.
    CHECK_FALSE(h.system.isPreloaded("tst_wheel"));
    h.system.preload("tst_wheel", "startScene");
    h.step();
    CHECK(h.lua == std::vector<std::string>{"startScene(4)", "startScene(4)"});
    REQUIRE(h.system.cache().find(4) != nullptr);
    CHECK(h.system.cache().find(4)->users == 1);
    REQUIRE(h.system.play(4, wheel));
    h.step();
    REQUIRE(h.system.frame(4).has_value());
    CHECK(h.system.frame(4).value_or(-1.0F) == Approx(0.0F).margin(0.01F));
    CHECK(h.system.stats().started == 2);
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

TEST_CASE("event 73 holds a character's intro card before the scene camera for its time", "[scenes]") {
    // docs/research/scenes.md#intro-cards: SceneTask_HoldObject puts the bound object k × aspect / tan(fov / 2) along
    // the camera's view (k 0.5 at 4:3, 0.3 at 16:9), turned as the camera, every update until its time is up; then
    // the object's own track takes it back underground.
    for (const bool wide : {false, true}) {
        Harness h;
        h.host.sixteenNine = wide;
        constexpr double kCard = 42.0;
        h.system.preload("tst_card", "");
        h.step();
        h.system.addObject(7, kCard, 0);
        REQUIRE(h.system.play(7, Harness::cinematic()));
        for (int i = 0; i < 4; ++i) {
            h.step();
        }
        REQUIRE(h.system.state(7) == scenes::SceneState::Playing);
        CHECK(h.host.objectPoses.at(kCard).position.z == Approx(-10.0F)); // parked before the event
        for (int i = 0; i < 10; ++i) {
            h.step();
        }
        const float distance = wide ? 0.3F * 1.6667F / std::tan(std::numbers::pi_v<float> / 6.0F)
                                    : 0.5F * 1.3333F / std::tan(std::numbers::pi_v<float> / 6.0F);
        const scenes::ScenePose& held = h.host.objectPoses.at(kCard);
        CHECK(held.position.x == Approx(0.0F).margin(1e-4));
        CHECK(held.position.y == Approx(-5.0F + distance).margin(1e-4));
        CHECK(held.position.z == Approx(2.0F).margin(1e-4));
        CHECK(held.rotation.w == Approx(1.0F).margin(1e-4));
        // A second later the hold is over and the track puts the card back.
        for (int i = 0; i < 32; ++i) {
            h.step();
        }
        CHECK(h.host.objectPoses.at(kCard).position.z == Approx(-10.0F));
    }
}

TEST_CASE("an intro card is held along the camera's own view axis", "[scenes]") {
    // A camera turned 90° about z looks along -x (its local +y): the card is d ahead of it that way, turned with it.
    const float half = std::numbers::sqrt2_v<float> / 2.0F;
    const scenes::ScenePose camera{.position = {1.0F, 2.0F, 3.0F}, .rotation = {0.0F, 0.0F, half, half}};
    const scenes::ScenePose card = scenes::heldObjectPose(camera, 90.0F, false);
    const float distance = 0.5F * 1.3333F; // tan(45°) = 1
    CHECK(card.position.x == Approx(1.0F - distance).margin(1e-4));
    CHECK(card.position.y == Approx(2.0F).margin(1e-4));
    CHECK(card.position.z == Approx(3.0F).margin(1e-4));
    CHECK(card.rotation.z == Approx(half));
}
