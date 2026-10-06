// SPDX-License-Identifier: GPL-3.0-or-later

// A check against the player's own disc: level99's two in-engine scenes, l99_c1 (the intro) and l99_c5 (before the
// sparring fight), load from the scene list with their segments and play headless to their ends with the roles
// level99_combat.lua binds (docs/research/scenes.md#superrunscene): every bound human starts on its role's start mark,
// ends on its end mark, and the scenes last their frame counts. It runs only when the environment variable
// CONEY_DISC names the disc and skips otherwise. It prints counts only, never game data (LEGAL.md).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>

#include "fileio/disc.h"
#include "fileio/wad.h"
#include "scenes/scene_disc.h"
#include "scenes/scene_player.h"

namespace scenes = coney::scenes;

namespace {

// A host that follows the bound humans: their first and last frames, and when they were released.
class FollowingHost final : public scenes::SceneHost {
  public:
    struct Human {
        std::optional<scenes::RoleFrame> first;
        scenes::RoleFrame last;
        std::optional<scenes::ScenePose> endPose;
        std::uint64_t frames = 0;
        bool released = false;
    };
    std::map<double, Human> humans;
    std::uint64_t cameraUpdates = 0;
    std::uint64_t fades = 0;
    std::uint64_t captions = 0;
    std::uint64_t sounds = 0;
    std::uint64_t soundtracks = 0;
    std::uint64_t soundtrackStarts = 0;

    void humanPose(double human, const scenes::RoleFrame& frame) override {
        Human& h = humans[human];
        if (!h.first) {
            h.first = frame;
        }
        h.last = frame;
        ++h.frames;
    }
    void humanRelease(double human, const std::optional<scenes::ScenePose>& endPose) override {
        humans[human].released = true;
        humans[human].endPose = endPose;
    }
    void cameraPose(const scenes::ScenePose& /*pose*/, const scenes::SceneLens& /*lens*/) override { ++cameraUpdates; }
    void screenEffect(scenes::ScreenEffect type, float /*seconds*/) override {
        fades += type == scenes::ScreenEffect::FadeIn || type == scenes::ScreenEffect::FadeOut ? 1 : 0;
    }
    void caption(std::string_view /*scene*/, int /*command*/) override { ++captions; }
    void sound(std::uint32_t /*hash*/, std::optional<double> /*object*/) override { ++sounds; }
    void soundtrackPrepare(std::uint32_t /*hash*/) override { ++soundtracks; }
    void soundtrackStart() override { ++soundtrackStarts; }
};

// The smallest angle between two headings.
float angleBetween(float a, float b) {
    const float d = std::remainder(a - b, 2.0F * 3.14159265F);
    return std::abs(d);
}

// What playing one scene to its end (or to a skip at `skipAt` updates) gave.
struct Played {
    std::uint64_t playingUpdates = 0;
    std::uint64_t segments = 0;
    float worstStart = 0.0F;     // metres from a role's start mark at its first frame
    float worstStartTurn = 0.0F; // radians from its start heading
    float worstEnd = 0.0F;       // metres from its end mark at its last frame (played out) or its release (skipped)
    std::size_t bound = 0;
    std::size_t released = 0;
    std::uint64_t endCalls = 0;
    std::unique_ptr<FollowingHost> host = std::make_unique<FollowingHost>();
};

// Plays `name` as level99's script binds it: handle 100 + i for each role i but `nilRole`; a cinematic as
// gPlayCutScene asks for one.
Played play(scenes::SceneSystem& system, const scenes::SceneList& list, std::string_view name, std::size_t nilRole,
            std::optional<std::uint64_t> skipAt = std::nullopt) {
    Played played;
    std::uint64_t endCalls = 0;
    system.setScriptCall([&endCalls](std::string_view function, std::span<const double>) {
        endCalls += function == "PreCashTheWorld" ? 1 : 0;
    });
    system.setHost(played.host.get());
    const std::uint32_t id = system.preload(name, "");
    REQUIRE(list.entry(id) != nullptr);
    std::uint64_t now = 1000;
    system.update(now, 0);
    REQUIRE(system.state(id) == scenes::SceneState::Loaded);
    const scenes::SceneHeader header = *system.cache().find(id)->header;
    const std::uint64_t segmentsBefore = system.cache().segmentsRead();
    for (std::size_t role = 0; role < header.roles.size(); ++role) {
        if (role != nilRole && system.joinHuman(100.0 + static_cast<double>(role), id, role, 0)) {
            ++played.bound;
        }
    }
    REQUIRE(system.play(id, scenes::PlayRequest{.kind = scenes::PlayKind::Cinematic,
                                                .onEnd = "PreCashTheWorld",
                                                .cinematic = true,
                                                .skippable = true,
                                                .blendCam = -1.0F}));
    for (std::uint64_t update = 0; update < 5000 && system.state(id) != scenes::SceneState::Empty; ++update) {
        now += 33;
        const bool skip = skipAt && update == *skipAt;
        system.update(now, skip ? scenes::kSkipCross : 0);
        played.playingUpdates += system.state(id) == scenes::SceneState::Playing ? 1 : 0;
    }
    played.segments = system.cache().segmentsRead() - segmentsBefore;
    played.endCalls = endCalls;
    for (const auto& [handle, human] : played.host->humans) {
        const auto role = static_cast<std::size_t>(handle - 100.0);
        const scenes::SceneRole& def = header.roles.at(role);
        if (human.first) {
            played.worstStart =
                std::max(played.worstStart, coney::anim::distance(human.first->feet, def.start.position));
            played.worstStartTurn = std::max(played.worstStartTurn,
                                             angleBetween(human.first->heading, scenes::headingOf(def.start.rotation)));
        }
        const coney::anim::Vec3 end = human.endPose ? human.endPose->position : human.last.feet;
        played.worstEnd = std::max(played.worstEnd, coney::anim::distance(end, def.end.position));
        played.released += human.released ? 1 : 0;
    }
    return played;
}

} // namespace

TEST_CASE("level99's scenes load and play headless to their ends", "[disc][scenes]") {
    const char* discPath = SDL_getenv("CONEY_DISC");
    if (discPath == nullptr || *discPath == '\0') {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto disc = coney::io::Disc::open(discPath);
    REQUIRE(disc.has_value());
    auto wad = coney::io::Wad::open(std::move(*disc));
    REQUIRE(wad.has_value());
    auto list = scenes::loadSceneList(*wad);
    REQUIRE(list.has_value());
    CHECK(list->size() == 2765);
    scenes::SceneSystem system(*list, scenes::wadSceneSource(*wad), {});

    // l99_c1: 2,026 frames in a header and five segments; role 3 is NilHandle's.
    const Played c1 = play(system, *list, "l99_c1", 3);
    std::printf(
        "l99_c1: %zu humans bound, %llu updates playing, %llu segments, start within %.4f m and %.4f rad, end "
        "within %.4f m, %llu camera updates, %llu fades, %llu captions, %llu sounds, soundtrack %llu prepared %llu "
        "started\n",
        c1.bound, static_cast<unsigned long long>(c1.playingUpdates), static_cast<unsigned long long>(c1.segments),
        c1.worstStart, c1.worstStartTurn, c1.worstEnd, static_cast<unsigned long long>(c1.host->cameraUpdates),
        static_cast<unsigned long long>(c1.host->fades), static_cast<unsigned long long>(c1.host->captions),
        static_cast<unsigned long long>(c1.host->sounds), static_cast<unsigned long long>(c1.host->soundtracks),
        static_cast<unsigned long long>(c1.host->soundtrackStarts));
    CHECK(c1.bound == 7);
    CHECK(c1.segments == 5);
    CHECK(c1.playingUpdates >= 2025);
    CHECK(c1.playingUpdates <= 2027);
    CHECK(c1.worstStart < 0.001F);
    CHECK(c1.worstStartTurn < 0.001F);
    CHECK(c1.worstEnd < 0.05F);
    CHECK(c1.released == 7);
    CHECK(c1.endCalls == 1);
    CHECK(c1.host->soundtracks == 1);
    CHECK(c1.host->soundtrackStarts == 1);

    // l99_c5: 500 frames in a header and one segment; role 5 is NilHandle's.
    const Played c5 = play(system, *list, "l99_c5", 5);
    std::printf("l99_c5: %zu humans bound, %llu updates playing, %llu segments, start within %.4f m and %.4f rad, end "
                "within %.4f m\n",
                c5.bound, static_cast<unsigned long long>(c5.playingUpdates),
                static_cast<unsigned long long>(c5.segments), c5.worstStart, c5.worstStartTurn, c5.worstEnd);
    CHECK(c5.bound == 7);
    CHECK(c5.segments == 1);
    CHECK(c5.playingUpdates >= 499);
    CHECK(c5.playingUpdates <= 501);
    CHECK(c5.worstStart < 0.001F);
    CHECK(c5.worstEnd < 0.05F);
    CHECK(c5.endCalls == 1);
    CHECK(c5.host->soundtracks == 1);
    CHECK(c5.host->soundtrackStarts == 1);

    // l99_c5 skipped with cross after 3 s: every human is put on its end mark.
    const Played skipped = play(system, *list, "l99_c5", 5, 90);
    std::printf("l99_c5 skipped: %llu updates playing, end within %.4f m\n",
                static_cast<unsigned long long>(skipped.playingUpdates), skipped.worstEnd);
    CHECK(skipped.playingUpdates < 100);
    CHECK(skipped.worstEnd < 0.001F);
    CHECK(skipped.released == 7);
    CHECK(skipped.endCalls == 1);
    CHECK(system.stats().skipped == 1);
}
