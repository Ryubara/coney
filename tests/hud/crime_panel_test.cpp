// SPDX-License-Identifier: GPL-3.0-or-later
#include "hud/crime_panel.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "hud/hud.h"
#include "support/hud_fixtures.h"
#include "support/recording_device.h"

using Catch::Approx;
using coney::hud::CrimePanel;

namespace {

// An untextured batch for the shapes.
coney::graphics::SpriteBatch shapesBatch() {
    coney::graphics::SpriteSheet sheet;
    sheet.texture = std::make_shared<coney::test::FakeTexture>(4, 4);
    return coney::graphics::SpriteBatch(sheet, 4, 13000.0F);
}

// The centred announcement's text, empty when none shows: a checked read clang-tidy can follow.
std::string centredText(const coney::hud::Hud& hud) {
    const std::optional<coney::hud::Announcement>& shown = hud.centredAnnouncement();
    return shown ? shown->text : std::string{};
}

} // namespace

TEST_CASE("the radar frame's fill is the timer's fraction, full above 0.9, eased 5 % an update", "[hud][crime]") {
    CHECK(CrimePanel::fillOf(0.0F) == 0.0F);
    CHECK(CrimePanel::fillOf(0.5F) == Approx(0.5F));
    CHECK(CrimePanel::fillOf(0.95F) == 1.0F);
    CrimePanel panel;
    // A jump over 0.5 snaps; then it eases a twentieth of the gap each update.
    panel.update(1.0F, 0.0F);
    CHECK(panel.wantedShown() == 1.0F);
    panel.update(0.8F, 0.0F);
    CHECK(panel.wantedShown() == Approx(0.99F));
    panel.update(0.6F, 0.0F);
    CHECK(panel.wantedShown() == Approx(0.99F - (0.39F * 0.05F)));
}

TEST_CASE("the wanted arcs grow up both sides of the radar from the bottom, the second timer outside them",
          "[hud][crime]") {
    coney::graphics::SpriteBatch batch = shapesBatch();
    const coney::graphics::OverlayPoint centre{0.5F, -0.3F, 1.0F};
    CrimePanel panel;
    // Nothing runs: nothing drawn.
    panel.update(0.0F, 0.0F);
    panel.render(batch, centre);
    CHECK(batch.triangles().empty());
    // Wanted, full: two arcs of 16 segments, two triangles each, in blue; the first vertex straight below the centre.
    panel.update(1.0F, 0.0F);
    panel.render(batch, centre);
    REQUIRE(batch.triangles().size() == std::size_t{2} * 16U * 2U * 3U);
    CHECK(batch.triangles()[0].colour == coney::hud::kWantedColour);
    CHECK(batch.triangles()[0].position.x == Approx(centre.x));
    CHECK(batch.triangles()[0].position.y < centre.y);
    // The right-hand arc's end is straight above, the band 54 to 57.6 pixels out.
    const coney::graphics::OverlayVertex& top = batch.triangles()[(16U * 6U) - 2U];
    CHECK(top.position.x == Approx(centre.x).margin(1e-4));
    CHECK(top.position.y > centre.y);
    // Both timers: the second pair outside the wanted pair, in orange.
    batch.clear();
    panel.update(1.0F, 1.0F);
    panel.render(batch, centre);
    REQUIRE(batch.triangles().size() == std::size_t{4} * 16U * 2U * 3U);
    CHECK(batch.triangles().back().colour == coney::hud::kSecondTimerColour);
    const float wantedOut = std::fabs(batch.triangles()[1].position.y - centre.y);
    const float secondIn = std::fabs(batch.triangles()[(std::size_t{2} * 16U * 6U)].position.y - centre.y);
    CHECK(secondIn == Approx(wantedOut));
}

TEST_CASE("the HUD draws player 0's radar frame from its gang's wanted time", "[hud][crime]") {
    coney::graphics::SpriteBatch shapes = shapesBatch();
    coney::hud::HudCanvas canvas;
    canvas.shapes = &shapes;
    coney::hud::HudServices services;
    float wanted = 1.0F;
    services.wantedTimers = [&wanted](std::size_t player, std::uint64_t) {
        return std::array<float, 2>{player == 0 ? wanted : 0.0F, 0.0F};
    };
    coney::hud::Hud hud(services);
    coney::hud::HudFrame frame;
    hud.update(frame);
    CHECK(hud.crimePanel().wantedShown() == Approx(1.0F));
    hud.render(canvas);
    CHECK(shapes.triangles().size() == std::size_t{2} * 16U * 2U * 3U);
    // Hidden, nothing is drawn.
    shapes.clear();
    hud.hideAll();
    hud.render(canvas);
    CHECK(shapes.triangles().empty());
}

TEST_CASE("a wanted message sounds the alarm once a spell and copies a new crime text to the centre", "[hud][crime]") {
    coney::test::KeepingAudio audio;
    coney::hud::HudServices services;
    services.sound = audio.sound;
    coney::hud::Hud hud(std::move(services));
    // The first show: the alarm and the centred copy.
    hud.setWanted(7, "assault");
    CHECK(hud.wanted());
    CHECK(audio.cues == std::vector<int>{coney::hud::kCueWanted});
    REQUIRE(hud.centredAnnouncement().has_value());
    CHECK(centredText(hud) == "assault");
    // A repeat while wanted: no alarm and the copy is not restarted, even after something else replaced it.
    hud.setAnnouncement(5, "other", false);
    hud.setWanted(7, "assault");
    CHECK(audio.cues.size() == 1);
    CHECK(centredText(hud) == "other");
    // A different crime text is copied, still without the alarm.
    hud.setWanted(8, "curfew");
    CHECK(audio.cues.size() == 1);
    CHECK(centredText(hud) == "curfew");
    // The wanted time runs out (0xb): cleared, the copy left; the next crime sounds the alarm and shows again.
    hud.setWanted(0xb, "");
    CHECK_FALSE(hud.wanted());
    CHECK(centredText(hud) == "curfew");
    hud.setWanted(7, "curfew");
    CHECK(audio.cues.size() == 2);
    CHECK(centredText(hud) == "curfew");
}
