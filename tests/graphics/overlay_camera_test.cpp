// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/overlay_camera.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using coney::graphics::LogicalPoint;
using coney::graphics::OverlayCamera;
using coney::graphics::OverlayPoint;

TEST_CASE("a GUI point becomes overlay-camera space as the device's conversion does", "[overlay_camera]") {
    // X = (x - 0.5) * 640 / 448, Y = 0.5 - y, at depth 1.1 (docs/research/graphics.md#2d-drawing).
    const OverlayPoint centre = OverlayCamera::guiToOverlay(0.5F, 0.5F);
    CHECK(centre == OverlayPoint{0.0F, 0.0F, 1.1F});
    const OverlayPoint corner = OverlayCamera::guiToOverlay(0.0F, 0.0F);
    CHECK(corner.x == Approx(-0.5 * 640.0 / 448.0));
    CHECK(corner.y == Approx(0.5));
    CHECK(OverlayCamera::guiWidthToOverlay(0.7F) == Approx(0.7 * 640.0 / 448.0));
}

TEST_CASE("the overlay camera's view window is 0.725 x 0.5 with the safe margin of the research page",
          "[overlay_camera]") {
    const OverlayCamera camera;
    CHECK(camera.viewWindowX() == Approx(0.725));
    CHECK(camera.viewWindowY() == Approx(0.5));
    // GUI 0 and 1 land about 5.2 % and 94.8 % across, 4.5 % and 95.5 % down (docs/research/graphics.md#2d-drawing).
    const LogicalPoint topLeft = camera.guiToLogical(0.0F, 0.0F);
    const LogicalPoint bottomRight = camera.guiToLogical(1.0F, 1.0F);
    CHECK(topLeft.x / 640.0F == Approx(0.0522).margin(5e-4));
    CHECK(topLeft.y / 448.0F == Approx(0.0455).margin(5e-4));
    CHECK(bottomRight.x / 640.0F == Approx(0.9478).margin(5e-4));
    CHECK(bottomRight.y / 448.0F == Approx(0.9545).margin(5e-4));
    // The GUI centre is the screen's centre.
    const LogicalPoint centre = camera.guiToLogical(0.5F, 0.5F);
    CHECK(centre.x == Approx(320.0));
    CHECK(centre.y == Approx(224.0));
}

TEST_CASE("the main menu's button glyph lands where PCSX2 shows it", "[overlay_camera]") {
    // The runtime check of docs/research/graphics.md#2d-drawing: overlay (-0.6945, -0.3700, -1.1) is drawn at 6.45 %
    // across and 83.5 % down.
    const OverlayCamera camera;
    const LogicalPoint glyph = camera.project(OverlayPoint{-0.6945F, -0.3700F, 1.1F});
    CHECK(glyph.x / 640.0F == Approx(0.0646).margin(5e-4));
    CHECK(glyph.y / 448.0F == Approx(0.8364).margin(5e-4));
}

TEST_CASE("GUI sizes project like the distance between GUI points, and shrink with depth", "[overlay_camera]") {
    const OverlayCamera camera;
    // A GUI box 0.1 wide and 0.2 high: width converted by W / H, height as it is.
    const LogicalPoint size =
        camera.projectSize(OverlayCamera::guiWidthToOverlay(0.1F), 0.2F, OverlayCamera::kGuiDepth);
    const LogicalPoint from = camera.guiToLogical(0.5F, 0.4F);
    const LogicalPoint to = camera.guiToLogical(0.6F, 0.6F);
    CHECK(size.x == Approx(to.x - from.x));
    CHECK(size.y == Approx(to.y - from.y));
    const LogicalPoint twiceAsFar = camera.projectSize(1.0F, 1.0F, 2.2F);
    const LogicalPoint near = camera.projectSize(1.0F, 1.0F, 1.1F);
    CHECK(twiceAsFar.x == Approx(near.x / 2));
}

TEST_CASE("the 16:9 mode's overlay camera sees 2.017 x 1.21 overlay units at the GUI depth", "[overlay_camera]") {
    // Scale 1.1, aspect 1.6667: a view window of 0.9167 x 0.55 (docs/research/graphics.md#video-mode).
    const OverlayCamera wide = OverlayCamera::forMode(true);
    CHECK(wide.viewWindowX() == Approx(0.9167).margin(1e-4));
    CHECK(wide.viewWindowY() == Approx(0.55));
    const LogicalPoint screen = wide.projectSize(2.017F, 1.21F, OverlayCamera::kGuiDepth);
    CHECK(screen.x == Approx(640.0).epsilon(1e-3));
    CHECK(screen.y == Approx(448.0).epsilon(1e-3));
    // 4:3 is the default camera.
    CHECK(OverlayCamera::forMode(false).viewWindowX() == Approx(OverlayCamera().viewWindowX()));
    CHECK(OverlayCamera::forMode(false).viewWindowY() == Approx(OverlayCamera().viewWindowY()));
}

TEST_CASE("unproject undoes project at the same depth", "[overlay_camera]") {
    const OverlayCamera camera(1.1F);
    const LogicalPoint point{100.0F, 300.0F};
    const OverlayPoint back = camera.unproject(point, 3.0F);
    CHECK(back.z == 3.0F);
    const LogicalPoint again = camera.project(back);
    CHECK(again.x == Approx(point.x));
    CHECK(again.y == Approx(point.y));
}

TEST_CASE("unprojectSize undoes projectSize at the same depth", "[overlay_camera]") {
    const OverlayCamera camera;
    const LogicalPoint size = camera.unprojectSize(LogicalPoint{64.0F, 32.0F}, 2.0F);
    const LogicalPoint again = camera.projectSize(size.x, size.y, 2.0F);
    CHECK(again.x == Approx(64.0));
    CHECK(again.y == Approx(32.0));
}
