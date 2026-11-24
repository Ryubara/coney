// SPDX-License-Identifier: GPL-3.0-or-later
// Radar_Update's blip steps (docs/research/gui.md#radar-update): a new blip's first show or hide, the enemy and
// police dots with their marks, the objective blips' height icons, type-12 blips, icons 25 and 26 and the police
// hint, on synthetic blips and positions.
#include "hud/radar.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "hud/hud.h"
#include "support/optional_value.h"

using Catch::Approx;
using coney::anim::Vec3;
using coney::hud::Hud;
using coney::hud::RadarBlip;
using coney::hud::RadarState;
using coney::hud::RadarStepContext;

namespace {

// A radar shown and on, its player at the origin at height 0, zoom 50 m.
RadarState openRadar() {
    RadarState radar;
    radar.view.known = true;
    radar.view.position = Vec3{0.0F, 0.0F, 0.0F};
    radar.zoom = 50.0F;
    return radar;
}

// A step context that finds every handle at `at`.
RadarStepContext everythingAt(Vec3 at) {
    RadarStepContext context;
    context.locate = [at](double) -> std::optional<Vec3> { return at; };
    return context;
}

// The one blip of `radar`.
RadarBlip& onlyBlip(RadarState& radar) {
    REQUIRE(radar.blips.size() == 1);
    return radar.blips.begin()->second;
}

// A HUD whose tutorial strings name themselves ("T8"), with the game's hints on.
coney::hud::Hud hintedHud() {
    coney::hud::HudServices services;
    services.tutorialString = [](std::uint32_t id) { return "T" + std::to_string(id); };
    coney::hud::Hud hud(std::move(services));
    hud.setGameTutorialText(true);
    return hud;
}

// One HUD step at `nowMs` in level `level`, the player at the origin.
void stepHud(coney::hud::Hud& hud, std::uint64_t nowMs, int level) {
    coney::hud::HudFrame frame;
    frame.nowMs = nowMs;
    frame.levelNumber = level;
    frame.radar.known = true;
    hud.update(frame);
}

} // namespace

TEST_CASE("a new blip of type 7, 10 or 12 shows on a shown radar; any other starts hidden", "[hud][radar]") {
    // Expected: gui.md#radar-update, confirmed (code)
    RadarState radar = openRadar();
    for (const int type : {7, 10, 12}) {
        RadarBlip blip{.type = type};
        coney::hud::settleRadarBlip(blip, radar);
        CHECK(blip.marked);
        CHECK(blip.dotShown);
    }
    // A type-9 slot is the player's arrow widget: not marked, and no dot of its own.
    RadarBlip player{.type = 9};
    coney::hud::settleRadarBlip(player, radar);
    CHECK_FALSE(player.marked);
    CHECK_FALSE(player.dotShown);
    for (const int type : {1, 2, 6, 8}) {
        RadarBlip blip{.type = type};
        coney::hud::settleRadarBlip(blip, radar);
        CHECK_FALSE(blip.marked);
        CHECK_FALSE(blip.dotShown);
    }
    // While the radar is hidden even an always-shown type starts hidden.
    radar.hidden = true;
    RadarBlip objective{.type = 10};
    coney::hud::settleRadarBlip(objective, radar);
    CHECK_FALSE(objective.dotShown);
}

TEST_CASE("each blip type draws in its batch: objectives over the rest, enemies and Warriors under", "[hud][radar]") {
    // Expected: gui.md#radar-icons, confirmed (code)
    CHECK(coney::hud::radarBatchDepth(10) == 9000);
    CHECK(coney::hud::radarBatchDepth(2) == 7000);
    CHECK(coney::hud::radarBatchDepth(3) == 6000);
    CHECK(coney::hud::radarBatchDepth(1) == 5000);
    CHECK(coney::hud::radarBatchDepth(12) == 5000);
    for (const int type : {6, 7, 8}) {
        CHECK(coney::hud::radarBatchDepth(type) == 1000);
    }
}

TEST_CASE("with the shipped radar flags an enemy inside the zoom shows marked or not, and hides beyond it",
          "[hud][radar]") {
    // Expected: gui.md#radar-update, confirmed (runtime), run r-20261115-radar-blip-steps
    RadarState radar = openRadar();
    CHECK(radar.keepShown);
    CHECK_FALSE(radar.edgeEnemies);
    radar.blips[1.0] = RadarBlip{.type = 6};
    coney::hud::stepRadarBlips(radar, everythingAt(Vec3{10.0F, 0.0F, 0.0F}));
    CHECK_FALSE(onlyBlip(radar).marked);
    CHECK(onlyBlip(radar).dotShown);
    // Beyond the zoom the enemy dot goes.
    coney::hud::stepRadarBlips(radar, everythingAt(Vec3{60.0F, 0.0F, 0.0F}));
    CHECK_FALSE(onlyBlip(radar).dotShown);
}

TEST_CASE("without +0x18 an enemy shows only while marked; a mark lasts 300 updates", "[hud][radar]") {
    // Expected: gui.md#radar-update, confirmed (runtime), run r-20261115-radar-blip-steps
    RadarState radar = openRadar();
    radar.keepShown = false;
    radar.blips[1.0] = RadarBlip{.type = 8};
    const RadarStepContext near = everythingAt(Vec3{10.0F, 0.0F, 0.0F});
    coney::hud::stepRadarBlips(radar, near);
    CHECK_FALSE(onlyBlip(radar).dotShown);
    coney::hud::markRadarBlip(onlyBlip(radar));
    CHECK(onlyBlip(radar).markTimer == 300);
    coney::hud::stepRadarBlips(radar, near);
    CHECK(onlyBlip(radar).dotShown);
    CHECK(onlyBlip(radar).markTimer == 299);
    for (int i = 0; i < 299; ++i) {
        coney::hud::stepRadarBlips(radar, near);
    }
    CHECK(onlyBlip(radar).markTimer == 0);
    CHECK(onlyBlip(radar).marked);
    // The update that finds the timer at 0 ends the mark and hides the dot.
    coney::hud::stepRadarBlips(radar, near);
    CHECK_FALSE(onlyBlip(radar).marked);
    CHECK_FALSE(onlyBlip(radar).dotShown);
}

TEST_CASE("a blip beyond the zoom that is not an enemy keeps its dot as it was", "[hud][radar]") {
    // Expected: gui.md#radar-update, confirmed (code)
    RadarState radar = openRadar();
    radar.blips[1.0] = RadarBlip{.type = 1};
    const RadarStepContext far = everythingAt(Vec3{0.0F, 80.0F, 0.0F});
    coney::hud::stepRadarBlips(radar, far);
    CHECK_FALSE(onlyBlip(radar).dotShown);
    // Once inside the zoom it shows, and stays shown when it leaves again.
    coney::hud::stepRadarBlips(radar, everythingAt(Vec3{0.0F, 20.0F, 0.0F}));
    CHECK(onlyBlip(radar).dotShown);
    coney::hud::stepRadarBlips(radar, far);
    CHECK(onlyBlip(radar).dotShown);
}

TEST_CASE("an objective over 1 m above or below within 20 m turns arrow 75 or 74 in its icon's colour, and back",
          "[hud][radar]") {
    // Expected: gui.md#radar-update, confirmed (runtime), run r-20261115-radar-blip-steps
    RadarState radar = openRadar();
    radar.blips[1.0] = RadarBlip{.type = 10, .icon = 27, .scale = 0.7F};
    coney::hud::stepRadarBlips(radar, everythingAt(Vec3{5.0F, 5.0F, 1.5F}));
    CHECK(onlyBlip(radar).icon == 75);
    CHECK(onlyBlip(radar).colour == coney::hud::kRadarRaisedRed);
    CHECK(onlyBlip(radar).scale == Approx(0.7F * 0.9F));
    // Still above: left as it is.
    coney::hud::stepRadarBlips(radar, everythingAt(Vec3{5.0F, 5.0F, 1.5F}));
    CHECK(onlyBlip(radar).scale == Approx(0.7F * 0.9F));
    // Level again: icon 27 back at its size, white.
    coney::hud::stepRadarBlips(radar, everythingAt(Vec3{5.0F, 5.0F, 0.5F}));
    CHECK(onlyBlip(radar).icon == 27);
    CHECK(onlyBlip(radar).colour == coney::graphics::kWhite);
    CHECK(onlyBlip(radar).scale == Approx(0.7F));
    // Below, with icon 22: arrow 74 in purple at 1.2.
    onlyBlip(radar).icon = 22;
    coney::hud::stepRadarBlips(radar, everythingAt(Vec3{-3.0F, 0.0F, -2.0F}));
    CHECK(onlyBlip(radar).icon == 74);
    CHECK(onlyBlip(radar).colour == coney::hud::kRadarRaisedPurple);
    CHECK(onlyBlip(radar).scale == Approx(0.7F * 1.2F));
    // Below but 20 m or more away (|dx| + |dy|): back to 22.
    coney::hud::stepRadarBlips(radar, everythingAt(Vec3{-12.0F, 8.0F, -2.0F}));
    CHECK(onlyBlip(radar).icon == 22);
    CHECK(onlyBlip(radar).scale == Approx(0.7F));
}

TEST_CASE("icon 28 above turns yellow arrow 75; a locked icon only changes colour", "[hud][radar]") {
    // Expected: gui.md#radar-update, confirmed (code)
    RadarState radar = openRadar();
    radar.blips[1.0] = RadarBlip{.type = 10, .icon = 28, .scale = 0.7F};
    coney::hud::stepRadarBlips(radar, everythingAt(Vec3{1.0F, 0.0F, 3.0F}));
    CHECK(onlyBlip(radar).icon == 75);
    CHECK(onlyBlip(radar).colour == coney::hud::kRadarRaisedYellow);
    radar.blips[1.0] = RadarBlip{.type = 10, .icon = 27, .scale = 0.7F, .iconLock = true};
    coney::hud::stepRadarBlips(radar, everythingAt(Vec3{1.0F, 0.0F, 3.0F}));
    CHECK(onlyBlip(radar).icon == 27);
    CHECK(onlyBlip(radar).scale == Approx(0.7F));
    CHECK(onlyBlip(radar).colour == coney::hud::kRadarRaisedRed);
}

TEST_CASE("type-12 blips are drawn grey with alpha 0xf0 every update", "[hud][radar]") {
    // Expected: gui.md#radar-update, confirmed (runtime), run r-20261115-radar-blip-steps
    RadarState radar = openRadar();
    radar.blips[1.0] = RadarBlip{.type = 12};
    coney::hud::stepRadarBlips(radar, everythingAt(Vec3{3.0F, 0.0F, 0.0F}));
    CHECK(onlyBlip(radar).colour == coney::graphics::Rgba{0x80, 0x80, 0x80, 0xf0});
    onlyBlip(radar).colour = coney::graphics::kWhite;
    coney::hud::stepRadarBlips(radar, everythingAt(Vec3{90.0F, 0.0F, 0.0F}));
    CHECK(onlyBlip(radar).colour == coney::graphics::Rgba{0x80, 0x80, 0x80, 0xf0});
}

TEST_CASE("icons 25 and 26 move to the top batch, hidden there while unmarked", "[hud][radar]") {
    // Expected: gui.md#radar-update, confirmed (code)
    RadarState radar = openRadar();
    // A secondary objective shown inside the zoom, then given icon 25 out beyond it: the move hides the unmarked dot.
    radar.blips[1.0] = RadarBlip{.type = 1};
    coney::hud::stepRadarBlips(radar, everythingAt(Vec3{0.0F, 20.0F, 0.0F}));
    REQUIRE(onlyBlip(radar).dotShown);
    onlyBlip(radar).icon = 25;
    coney::hud::stepRadarBlips(radar, everythingAt(Vec3{0.0F, 70.0F, 0.0F}));
    CHECK(onlyBlip(radar).depth == 10000);
    CHECK_FALSE(onlyBlip(radar).dotShown);
    // A Warrior's (always marked) moves shown.
    radar.blips[1.0] = RadarBlip{.type = 7, .icon = 26};
    coney::hud::stepRadarBlips(radar, everythingAt(Vec3{0.0F, 70.0F, 0.0F}));
    CHECK(onlyBlip(radar).depth == 10000);
    CHECK(onlyBlip(radar).dotShown);
}

TEST_CASE("the player arrow is 0.03 and turned, but 0.035 and upright as icon 25 or 26", "[hud][radar]") {
    // Expected: gui.md#radar-update, confirmed (code)
    const coney::hud::RadarArrowLook usual = coney::hud::radarPlayerArrowLook(362);
    CHECK(usual.size == Approx(0.03F));
    CHECK(usual.turned);
    for (const int icon : {25, 26}) {
        const coney::hud::RadarArrowLook down = coney::hud::radarPlayerArrowLook(icon);
        CHECK(down.size == Approx(0.035F));
        CHECK_FALSE(down.turned);
    }
}

TEST_CASE("a fixed-point blip stays where it was put", "[hud][radar]") {
    // Expected: gui.md#radar-update, confirmed (code)
    RadarState radar = openRadar();
    radar.blips[1.0] = RadarBlip{.type = 7, .fixedPlace = Vec3{4.0F, 5.0F, 0.0F}};
    coney::hud::stepRadarBlips(radar, everythingAt(Vec3{30.0F, 0.0F, 0.0F}));
    REQUIRE(onlyBlip(radar).place.has_value());
    CHECK(coney::test::got(onlyBlip(radar).place).x == Approx(4.0F));
}

TEST_CASE("a blip whose object can no longer be found keeps its slot but has no place to be drawn", "[hud][radar]") {
    // Expected: gui.md#radar-update, confirmed (code)
    RadarState radar = openRadar();
    radar.blips[1.0] = RadarBlip{.type = 7};
    coney::hud::stepRadarBlips(radar, everythingAt(Vec3{0.0F, 10.0F, 0.0F}));
    REQUIRE(onlyBlip(radar).place.has_value());
    RadarStepContext gone;
    gone.locate = [](double) -> std::optional<Vec3> { return std::nullopt; };
    coney::hud::stepRadarBlips(radar, gone);
    CHECK(radar.blips.size() == 1);
    CHECK_FALSE(onlyBlip(radar).place.has_value());
}

TEST_CASE("a type-9 blip with no dot of its own ends the step, so the blips after it are not stepped", "[hud][radar]") {
    // Expected: gui.md#radar-update, confirmed (code)
    RadarState radar = openRadar();
    radar.blips[1.0] = RadarBlip{.type = 9};
    radar.blips[2.0] = RadarBlip{.type = 12};
    coney::hud::stepRadarBlips(radar, everythingAt(Vec3{3.0F, 0.0F, 0.0F}));
    CHECK(radar.blips.at(2.0).colour == coney::graphics::kWhite);
    CHECK_FALSE(radar.blips.at(2.0).place.has_value());
}

TEST_CASE("the HUD's radar on and off show and hide every dot; HideHud hides them until RestoreHud", "[hud][radar]") {
    // Expected: gui.md#radar-update, confirmed (code)
    coney::hud::Hud hud;
    hud.setRadarLocator([](double) -> std::optional<Vec3> { return Vec3{0.0F, 80.0F, 0.0F}; });
    // The first step turns the radars on by themselves; a secondary objective added after starts hidden and stays
    // so beyond the zoom...
    stepHud(hud, 0, 99);
    hud.radar().blips[1.0] = RadarBlip{.type = 1};
    stepHud(hud, 33, 99);
    CHECK_FALSE(hud.radar().blips.at(1.0).dotShown);
    // ...until the radar is turned on again, which shows every dot.
    hud.radarOff(0);
    hud.radarOn(0);
    stepHud(hud, 66, 99);
    CHECK(hud.radar().blips.at(1.0).dotShown);
    hud.hideAll();
    CHECK(hud.radar().hidden);
    CHECK_FALSE(hud.radar().blips.at(1.0).dotShown);
    hud.showAll();
    CHECK_FALSE(hud.radar().hidden);
    CHECK(hud.radar().blips.at(1.0).dotShown);
}

TEST_CASE("turning one radar on arms the automatic return, so the next step leaves the other off", "[hud][radar]") {
    // Expected: gui.md#radar-update, confirmed (code)
    coney::hud::Hud hud;
    hud.radarOff(2);
    hud.radarOn(0);
    stepHud(hud, 0, 99);
    CHECK(hud.radar().on[0]);
    CHECK_FALSE(hud.radar().on[1]);
}

TEST_CASE("the scanner's mark on a blip goes through the HUD to the layer-0 blip", "[hud][radar]") {
    // Expected: gui.md#radar-update, confirmed (code)
    coney::hud::Hud hud;
    hud.radar().blips[3.0] = RadarBlip{.type = 6};
    hud.markRadarBlip(3.0);
    CHECK(hud.radar().blips.at(3.0).marked);
    CHECK(hud.radar().blips.at(3.0).markTimer == 300);
    // A handle with no blip is left alone.
    hud.markRadarBlip(4.0);
    CHECK(hud.radar().blips.size() == 1);
}

TEST_CASE("a police dot within 0.9 of the zoom queues hint 8 once when the officer's brain is type 1", "[hud][radar]") {
    // Expected: gui.md#radar-update, confirmed (code)
    coney::hud::Hud hud = hintedHud();
    hud.setRadarLocator([](double handle) -> std::optional<Vec3> {
        return handle == 1.0 ? Vec3{0.0F, 30.0F, 0.0F} : Vec3{0.0F, 46.0F, 0.0F};
    });
    hud.setRadarBrainLookup([](double) -> std::optional<int> { return 1; });
    // At 46 m of a 50 m zoom (beyond 45) nothing.
    hud.radar().blips[2.0] = RadarBlip{.type = 8};
    stepHud(hud, 0, 98);
    CHECK_FALSE(hud.hints().showing().has_value());
    // At 30 m: hint 8, then never again.
    hud.radar().blips[1.0] = RadarBlip{.type = 8};
    stepHud(hud, 33, 98);
    REQUIRE(hud.hints().showing().has_value());
    CHECK(coney::test::got(hud.hints().showing()).text == "T8");
    hud.hints().clearShowing();
    stepHud(hud, 66, 98);
    CHECK_FALSE(hud.hints().showing().has_value());
    CHECK(hud.hints().queued().empty());
}

TEST_CASE("no police hint in levels 80 and 99, for another brain, or with the game's hints off", "[hud][radar]") {
    // Expected: gui.md#radar-update, confirmed (code)
    const auto run = [](int level, int brain, bool hints) {
        coney::hud::Hud hud = hintedHud();
        hud.setGameTutorialText(hints);
        hud.setRadarLocator([](double) -> std::optional<Vec3> { return Vec3{0.0F, 10.0F, 0.0F}; });
        hud.setRadarBrainLookup([brain](double) -> std::optional<int> { return brain; });
        hud.radar().blips[1.0] = RadarBlip{.type = 8};
        stepHud(hud, 0, level);
        return hud.hints().showing().has_value();
    };
    CHECK(run(98, 1, true));
    CHECK_FALSE(run(80, 1, true));
    CHECK_FALSE(run(99, 1, true));
    CHECK_FALSE(run(98, 2, true));
    CHECK_FALSE(run(98, 1, false));
}

// Expected: unverified, #1537 (Coney's own network play; the blips' fields are the original's slot's,
// gui.md#radar-blip-slots)
TEST_CASE("a guest's blip that follows a human is placed by the follow locator, never by its handle",
          "[hud][radar][net]") {
    RadarState radar = openRadar();
    radar.blips[1.0] = RadarBlip{.type = 7, .follows = 5};
    radar.blips[2.0] = RadarBlip{.type = 7, .follows = 6};
    radar.blips[3.0] = RadarBlip{.type = 7, .fixedPlace = Vec3{4.0F, 5.0F, 6.0F}, .follows = 5};
    RadarStepContext context;
    // The handle lookup finds everything, as the host's does: a guest's blip must not be found through it.
    context.locate = [](double) -> std::optional<Vec3> { return Vec3{99.0F, 99.0F, 99.0F}; };
    context.locateFollowed = [](std::uint32_t id) -> std::optional<Vec3> {
        return id == 5 ? std::optional<Vec3>(Vec3{0.0F, 10.0F, 0.0F}) : std::nullopt;
    };
    coney::hud::stepRadarBlips(radar, context);
    REQUIRE(radar.blips.at(1.0).place.has_value());
    CHECK(coney::test::got(radar.blips.at(1.0).place) == Vec3{0.0F, 10.0F, 0.0F});
    // A human the guest's world does not hold leaves the blip without a place, so it is not drawn.
    CHECK_FALSE(radar.blips.at(2.0).place.has_value());
    // A fixed place wins over the id.
    CHECK(coney::test::got(radar.blips.at(3.0).place) == Vec3{4.0F, 5.0F, 6.0F});
    // With no follow locator at all the blip has no place either.
    context.locateFollowed = {};
    coney::hud::stepRadarBlips(radar, context);
    CHECK_FALSE(radar.blips.at(1.0).place.has_value());
}

// Expected: unverified, #1537 (Coney's own view of the radar's blips for network play, snapshots.md#hud-blips)
TEST_CASE("the HUD's standing state holds the radar's blips by handle, and a guest's HUD takes them",
          "[hud][radar][net]") {
    Hud host;
    host.radar().blips[7.0] = RadarBlip{.type = 10, .icon = 27, .scale = 0.7F, .flashCountdown = 100};
    host.radar().blips[9.0] = RadarBlip{.type = 7,
                                        .icon = 365,
                                        .scale = 0.56F,
                                        .colour = coney::graphics::Rgba{1, 2, 3, 4},
                                        .flashing = true,
                                        .iconLock = true,
                                        .markTimer = 300,
                                        .dotShown = true,
                                        .follows = 0};
    const coney::hud::HudStanding standing = host.standing();
    REQUIRE(standing.shared.blips.size() == 2);
    CHECK(standing.shared.blips.at(7.0).icon == 27);
    CHECK(standing.shared.blips.at(9.0).colour == coney::graphics::Rgba{1, 2, 3, 4});

    // What a guest's HUD takes is what the standing state carries of each blip; the rest is its own.
    Hud guest;
    guest.applyStanding(standing);
    REQUIRE(guest.radar().blips.size() == 2);
    const RadarBlip& objective = guest.radar().blips.at(7.0);
    CHECK(objective.type == 10);
    CHECK(objective.icon == 27);
    CHECK(objective.scale == Approx(0.7F));
    CHECK(objective.flashCountdown == 100);
    const RadarBlip& human = guest.radar().blips.at(9.0);
    CHECK(human.icon == 365);
    CHECK(human.scale == Approx(0.56F));
    CHECK(human.colour == coney::graphics::Rgba{1, 2, 3, 4});
    CHECK(human.flashing);
    CHECK(human.iconLock);
    CHECK(human.markTimer == 300);
    CHECK(human.dotShown);
    // A new blip is set up as Radar_AddBlip leaves one (its batch by its type), with no first show or hide to redo,
    // since the host's dot state travels.
    CHECK(objective.settled);
    CHECK(objective.depth == 9000);
    CHECK(human.depth == 1000);
    CHECK(human.dotShown);

    // A blip the host no longer holds goes; one kept keeps what the guest's step found, and takes what changed.
    guest.radar().blips.at(9.0).place = Vec3{1.0F, 2.0F, 3.0F};
    host.radar().blips.erase(7.0);
    host.radar().blips.at(9.0).icon = 366;
    guest.applyStanding(host.standing());
    REQUIRE(guest.radar().blips.size() == 1);
    CHECK(guest.radar().blips.at(9.0).icon == 366);
    CHECK(coney::test::got(guest.radar().blips.at(9.0).place) == Vec3{1.0F, 2.0F, 3.0F});
    CHECK(guest.standing().shared.blips.size() == 1);
}

// Expected: unverified, #1537 (Coney's own network play)
TEST_CASE(
    "a guest's blips follow a human through the HUD's step, and a type-9 blip ends the update as it does the host's",
    "[hud][radar][net]") {
    Hud guest;
    guest.setRadarFollowLocator([](std::uint32_t id) -> std::optional<Vec3> {
        return id == 42 ? std::optional<Vec3>(Vec3{0.0F, 20.0F, 0.0F}) : std::nullopt;
    });
    coney::hud::HudStanding standing;
    standing.shared.blips[3.0] = RadarBlip{.type = 7, .follows = 42};
    standing.shared.blips[4.0] = RadarBlip{.type = 7, .follows = 43};
    standing.shared.blips[5.0] = RadarBlip{.type = 7, .fixedPlace = Vec3{0.0F, 30.0F, 0.0F}};
    guest.applyStanding(standing);
    stepHud(guest, 0, 99);
    stepHud(guest, 33, 99);
    CHECK(coney::test::got(guest.radar().blips.at(3.0).place) == Vec3{0.0F, 20.0F, 0.0F});
    CHECK_FALSE(guest.radar().blips.at(4.0).place.has_value());
    CHECK(coney::test::got(guest.radar().blips.at(5.0).place) == Vec3{0.0F, 30.0F, 0.0F});
    // Taking the same standing state again does not undo what the step found.
    guest.applyStanding(standing);
    CHECK(guest.radar().blips.at(3.0).place.has_value());
}
