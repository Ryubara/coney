// SPDX-License-Identifier: GPL-3.0-or-later
// The World page (src/debug/pages_world.cpp, docs/guides/debug-menu.md#world): the lighting's layers and the look, the
// fog, the shadows, the gravity and the world's parts, each with a reset, over a fake play mode with a synthetic
// level's lighting frame, driven by a scripted pad through the input gate as a player drives the menu and drawn by both
// front ends from the one model. No game, no disc.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <imgui.h>

#include "core/pad.h"
#include "debug/debug_session.h"
#include "debug/game_tunables.h"
#include "debug/menu_model.h"
#include "debug/play_controls.h"
#include "debug/tunables.h"
#include "graphics/level_lighting.h"
#include "graphics/lighting_frame.h"
#include "graphics/lighting_switches.h"
#include "gui/debug_menu_view.h"
#include "human/human.h"
#include "human/humans.h"
#include "platform/imgui_model_view.h"
#include "support/recording_device.h"

using Catch::Approx;
using coney::debug::DebugServices;
using coney::debug::DebugSession;
using coney::debug::ItemKind;
using coney::debug::MenuItem;
using coney::debug::MenuPage;
using coney::debug::TunableRegistry;
using coney::graphics::Rgba;
namespace pad = coney::pad;

namespace {

// A level whose scripts set a grey-blue fog from 0.4 of the far clip and the Xbox's default shadow light.
coney::graphics::LightingFrame levelFrame() {
    coney::graphics::LevelLighting level;
    level.fog.colour = Rgba{10, 20, 30, 255};
    level.fog.start = 0.4F;
    level.shadow.setColour(0.7F, 0.7F, 0.7F);
    level.shadow.setOffset(-500.0F, -500.0F, 850.0F);
    level.casters.setEnabled(7, false);
    const coney::graphics::LightView view{
        .position = {}, .forward = {0.0F, 0.0F, 1.0F}, .nearClip = 0.5F, .farClip = 115.0F};
    level.lights.beginViewport(view, 0);
    return coney::graphics::buildLightingFrame(level, view);
}

// A play mode that offers the level's lighting frame; everything else is a dummy.
class FakePlay final : public coney::debug::PlayControls {
  public:
    [[nodiscard]] std::string sceneName() const override { return "world test"; }
    [[nodiscard]] coney::anim::Vec3 playerFeet() const override { return {}; }
    [[nodiscard]] float playerHeadingDegrees() const override { return 0.0F; }
    [[nodiscard]] float playerSpeed() const override { return 0.0F; }
    [[nodiscard]] std::string playerState() const override { return "-"; }
    void teleport(const coney::debug::Place& /*place*/) override {}
    [[nodiscard]] std::vector<coney::debug::Place> places() const override { return {}; }
    [[nodiscard]] bool playerFrozen() const override { return false; }
    void setPlayerFrozen(bool /*frozen*/) override {}
    [[nodiscard]] coney::anim::Vec3 cameraEye() const override { return {}; }
    [[nodiscard]] coney::anim::Vec3 cameraTarget() const override { return {}; }
    void resetCamera() override {}
    [[nodiscard]] bool freeCamera() const override { return false; }
    void setFreeCamera(bool /*on*/) override {}
    [[nodiscard]] bool canSpawn() const override { return false; }
    std::expected<void, coney::Error> spawn(const coney::sandbox::Primitive& /*primitive*/) override { return {}; }
    [[nodiscard]] std::size_t spawnedCount() const override { return 0; }
    std::expected<void, coney::Error> clearSpawned() override { return {}; }
    [[nodiscard]] const coney::graphics::LightingFrame* lightingFrame() const override { return &frame; }

    coney::graphics::LightingFrame frame = levelFrame();
};

// An input source that gives back whatever the test put in `next`.
class FakeInput final : public coney::InputSource {
  public:
    coney::PortSamples next{};
    coney::PortSamples sample(std::uint64_t /*frame*/) override { return next; }
};

// A painter that keeps every text it is asked to draw, at a fixed 6 x 10 cell.
class RecordingPainter final : public coney::gui::DebugTextPainter {
  public:
    [[nodiscard]] float lineHeight() const override { return 10.0F; }
    [[nodiscard]] float measure(std::string_view text) const override { return static_cast<float>(text.size()) * 6.0F; }
    void text(std::string_view text, float /*x*/, float /*y*/, Rgba /*colour*/) override { texts.emplace_back(text); }
    void flush(coney::graphics::RenderDevice& /*device*/) override {}

    // Whether some text drawn holds `wanted`.
    [[nodiscard]] bool drew(std::string_view wanted) const {
        return std::ranges::any_of(texts,
                                   [wanted](const std::string& t) { return t.find(wanted) != std::string::npos; });
    }

    std::vector<std::string> texts;
};

// The item labelled `label` on `page`, which must exist.
const MenuItem& itemOn(const MenuPage& page, std::string_view label) {
    const MenuItem* item = page.find(label);
    INFO(label);
    REQUIRE(item != nullptr);
    return *item;
}

// The page the submenu `label` of `page` opens.
std::shared_ptr<MenuPage> open(const MenuPage& page, std::string_view label) {
    const MenuItem& item = itemOn(page, label);
    REQUIRE(item.kind == ItemKind::Submenu);
    return item.open();
}

// The game's tunables, put back at their defaults when the test ends: they write the variables the game reads.
struct GameTunables {
    GameTunables() { coney::debug::registerGameTunables(registry); }
    ~GameTunables() {
        registry.resetAll();
        static_cast<void>(registry.applyPending());
    }
    GameTunables(const GameTunables&) = delete;
    GameTunables& operator=(const GameTunables&) = delete;
    GameTunables(GameTunables&&) = delete;
    GameTunables& operator=(GameTunables&&) = delete;

    TunableRegistry registry;
};

} // namespace

TEST_CASE("the World page sets the fog, the shadows, the gravity and the parts, and resets each",
          "[debug][world_page]") {
    // Expected: unverified, #216 (Coney's own debug menu: the World page, docs/guides/debug-menu.md#world); the level's
    // values shown are the original's: world.md#fog (SetFogColor, SetFogDistance), xbox-lighting.md#shadow-light
    // (SetShadowColor, SetShadowLightOffset), characters.md (gravity 15.68 m/s², Human_StateUpdate 0x0023fea8)
    GameTunables tunables;
    FakePlay play;
    DebugServices services;
    services.play = [&play]() -> coney::debug::PlayControls* { return &play; };
    DebugSession session(tunables.registry, services, nullptr);
    const auto titles = session.model().pageTitles();
    REQUIRE(std::ranges::find(titles, "World") != titles.end());
    const auto page = session.model().openPage("World");
    REQUIRE(page != nullptr);
    for (const std::string_view group : {"Lighting", "Fog", "Shadows", "Parts"}) {
        INFO(group);
        CHECK(itemOn(*page, group).kind == ItemKind::Submenu);
    }
    CHECK(itemOn(*page, "Look").kind == ItemKind::Choice);
    CHECK(itemOn(*page, "Reset world").kind == ItemKind::Action);

    // Lighting: the Lighting page's layers, the same switches, with a reset.
    const auto lighting = open(*page, "Lighting");
    itemOn(*lighting, "Point lights").setBool(false);
    CHECK_FALSE(session.lighting().layers.pointLights);
    itemOn(*lighting, "Reset lighting").run();
    CHECK(session.lighting().layers == coney::graphics::LightingLayers{});

    // Fog: the level's, then a start and a colour of the page's own; back at the level's value it is the level's again.
    const auto fog = open(*page, "Fog");
    CHECK(coney::debug::valueText(itemOn(*fog, "Level's fog")) == "(10, 20, 30) from 0.40");
    CHECK(itemOn(*fog, "Start").getNumber() == Approx(0.4));
    itemOn(*fog, "Start").setNumber(0.2);
    REQUIRE(session.lighting().fog.start.has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): the REQUIRE above ends the test when empty
    CHECK(*session.lighting().fog.start == Approx(0.2F));
    CHECK(itemOn(*fog, "Start").getNumber() == Approx(0.2));
    CHECK(coney::debug::resetItem(itemOn(*fog, "Start")));
    CHECK_FALSE(session.lighting().fog.start.has_value());
    itemOn(*fog, "Red").setNumber(200.0);
    REQUIRE(session.lighting().fog.colour.has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): the REQUIRE above ends the test when empty
    CHECK(*session.lighting().fog.colour == Rgba{200, 20, 30, 255});
    CHECK(itemOn(*fog, "Red").getNumber() == Approx(200.0));
    itemOn(*fog, "Start").setNumber(0.9);
    itemOn(*fog, "Reset fog").run();
    CHECK(session.lighting().fog == coney::graphics::FogSwitches{});

    // Shadows: the blob and the level's shadow light, which nothing draws a shadow map from yet.
    const auto shadows = open(*page, "Shadows");
    CHECK(coney::debug::valueText(itemOn(*shadows, "Shadow colour")) == "(0.70, 0.70, 0.70)");
    CHECK(coney::debug::valueText(itemOn(*shadows, "Shadow offset")) == "(-500, -500, 850)");
    CHECK(coney::debug::valueText(itemOn(*shadows, "Shadows off")) == "1");
    CHECK(coney::debug::valueText(itemOn(*shadows, "Shadow maps")).find("not drawn") != std::string::npos);
    itemOn(*shadows, "Blob shadows").setBool(false);
    CHECK_FALSE(session.lighting().layers.blobShadows);
    itemOn(*shadows, "Reset shadows").run();
    CHECK(session.lighting().layers.blobShadows);

    // Gravity: the humans' pull, queued like every tunable and applied between two steps.
    const MenuItem& gravity = itemOn(*page, "Gravity");
    CHECK(gravity.getNumber() == Approx(15.68));
    REQUIRE(gravity.defaultValue.has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): the REQUIRE above ends the test when empty
    CHECK(*gravity.defaultValue == Approx(15.68));
    CHECK(gravity.help.find("original's") != std::string::npos);
    gravity.setNumber(5.0);
    CHECK(coney::human::gravityTuning().gravity == Approx(coney::human::Human::kGravity));
    static_cast<void>(tunables.registry.applyPending());
    CHECK(coney::human::gravityTuning().gravity == Approx(5.0F));

    // Parts: each one off, then all on again.
    const auto parts = open(*page, "Parts");
    for (const std::string_view part : {"Streamed world", "Background", "Humans", "Objects", "Glass", "Water",
                                        "Effects", "Health rings", "Coronas", "Blob shadows"}) {
        INFO(part);
        const MenuItem& item = itemOn(*parts, part);
        REQUIRE(item.kind == ItemKind::Toggle);
        CHECK(item.getBool());
        item.setBool(false);
        CHECK_FALSE(item.getBool());
        CHECK(item.help.find("Coney's") != std::string::npos);
    }
    CHECK_FALSE(session.lighting().parts.scenery);
    CHECK_FALSE(session.lighting().parts.rings);
    CHECK_FALSE(session.lighting().layers.coronas);
    itemOn(*parts, "All on").run();
    CHECK(session.lighting().parts == coney::graphics::WorldParts{});
    CHECK(session.lighting().layers.coronas);

    // Reset world: everything the page changes, back as the game has it.
    session.lighting().parts.water = false;
    session.lighting().fog.start = 0.1F;
    session.lighting().layers.flicker = false;
    itemOn(*page, "Reset world").run();
    static_cast<void>(tunables.registry.applyPending());
    CHECK(session.lighting().faithfulPicture());
    CHECK(coney::human::gravityTuning().gravity == Approx(coney::human::Human::kGravity));
}

TEST_CASE("the World page says so where no level is drawn and no gravity is registered", "[debug][world_page]") {
    // Expected: unverified, #216 (Coney's own debug menu: a page whose service is missing says so)
    TunableRegistry tunables;
    DebugSession session(tunables, DebugServices{}, nullptr);
    const auto page = session.model().openPage("World");
    REQUIRE(page != nullptr);
    CHECK(coney::debug::valueText(itemOn(*open(*page, "Fog"), "Level's fog")) == "no level is drawn");
    CHECK(coney::debug::valueText(itemOn(*open(*page, "Shadows"), "Shadow colour")) == "no level is drawn");
    CHECK(coney::debug::valueText(itemOn(*open(*page, "Shadows"), "Shadow offset")) == "no level is drawn");
    CHECK(itemOn(*page, "Gravity").kind == ItemKind::Watch);
}

TEST_CASE("a scripted pad hides the humans on the World page and resets the world", "[debug][world_page]") {
    // Expected: unverified, #216 (Coney's own debug menu, driven as docs/guides/debug-menu.md#controls says)
    GameTunables tunables;
    FakePlay play;
    FakeInput input;
    DebugServices services;
    services.play = [&play]() -> coney::debug::PlayControls* { return &play; };
    DebugSession session(tunables.registry, services, &input);
    std::uint64_t frame = 0;
    // One frame through the gate with port 1 holding `buttons`, then one with nothing held, so the next press is new.
    const auto press = [&](std::uint16_t buttons) {
        input.next[0].connected = true;
        input.next[0].buttons = buttons;
        static_cast<void>(session.gate().sample(frame++));
        input.next[0].buttons = 0;
        static_cast<void>(session.gate().sample(frame++));
    };
    // The cursor up or down to the item labelled `label` on the page open now.
    const auto moveTo = [&](std::string_view label) {
        const auto& items = session.navigator().page()->items();
        const auto at = std::ranges::find_if(items, [label](const MenuItem& item) { return item.label == label; });
        REQUIRE(at != items.end());
        const auto target = static_cast<std::size_t>(at - items.begin());
        for (int i = 0; i < 64 && session.navigator().cursor() != target; ++i) {
            press(session.navigator().cursor() < target ? pad::kDown : pad::kUp);
        }
        REQUIRE(session.navigator().cursor() == target);
    };

    // L3 + R3 opens the menu; down to World (after Favourites), cross opens it.
    press(pad::kL3 | pad::kR3);
    REQUIRE(session.navigator().isOpen());
    const auto titles = session.model().pageTitles();
    for (std::ptrdiff_t i = 0; i < std::ranges::find(titles, "World") - titles.begin() + 1; ++i) {
        press(pad::kDown);
    }
    press(pad::kCross);
    REQUIRE(session.navigator().page() != nullptr);
    CHECK(session.navigator().page()->title() == "World");

    // Parts, then Humans: cross switches them off.
    moveTo("Parts");
    press(pad::kCross);
    CHECK(session.navigator().page()->title() == "Parts");
    moveTo("Humans");
    press(pad::kCross);
    CHECK_FALSE(session.lighting().parts.humans);

    // Back to World; the gravity raised with the d-pad, which lands between two steps.
    press(pad::kCircle);
    CHECK(session.navigator().page()->title() == "World");
    moveTo("Gravity");
    press(pad::kRight);
    static_cast<void>(tunables.registry.applyPending());
    CHECK(coney::human::gravityTuning().gravity > coney::human::Human::kGravity);
    // Triangle gives the original's gravity back.
    press(pad::kTriangle);
    static_cast<void>(tunables.registry.applyPending());
    CHECK(coney::human::gravityTuning().gravity == Approx(coney::human::Human::kGravity));

    // Reset world, further down the page.
    moveTo("Reset world");
    session.lighting().fog.start = 0.2F;
    press(pad::kCross);
    CHECK(session.lighting().parts.humans);
    CHECK(session.lighting().faithfulPicture());
}

TEST_CASE("both debug-menu front ends draw the World page from the one model", "[debug][world_page]") {
    // Expected: docs/guides/debug-menu.md#adding-a-feature
    GameTunables tunables;
    FakePlay play;
    DebugServices services;
    services.play = [&play]() -> coney::debug::PlayControls* { return &play; };
    DebugSession session(tunables.registry, services, nullptr);

    // The pad menu, opened on World/Fog.
    auto& nav = session.navigator();
    nav.open();
    const auto titles = session.model().pageTitles();
    const auto index = std::ranges::find(titles, "World") - titles.begin() + 1;
    for (std::ptrdiff_t i = 0; i < index; ++i) {
        nav.apply(coney::debug::MenuInputFrame{.action = coney::debug::MenuAction::Down});
    }
    nav.apply(coney::debug::MenuInputFrame{.action = coney::debug::MenuAction::Accept});
    REQUIRE(nav.page() != nullptr);
    const auto& items = nav.page()->items();
    const auto fog = std::ranges::find_if(items, [](const MenuItem& item) { return item.label == "Fog"; });
    REQUIRE(fog != items.end());
    for (std::ptrdiff_t i = 0; i < fog - items.begin(); ++i) {
        nav.apply(coney::debug::MenuInputFrame{.action = coney::debug::MenuAction::Down});
    }
    nav.apply(coney::debug::MenuInputFrame{.action = coney::debug::MenuAction::Accept});
    REQUIRE(nav.page() != nullptr);
    CHECK(nav.page()->title() == "Fog");
    RecordingPainter painter;
    coney::test::RecordingDevice device;
    coney::gui::DebugMenuView view;
    view.draw(session, painter, device);
    CHECK(painter.drew("Debug > World > Fog"));
    CHECK(painter.drew("(10, 20, 30) from 0.40"));
    // Every value the World page and its own groups show fits the pad menu's value column whole, so none is cut. The
    // Lighting group is the Lighting page's items, whose values that page's tests own.
    const float valueRoom = (coney::gui::DebugMenuView::kPanelWidth - 2.0F * coney::gui::DebugMenuView::kPadding) *
                            coney::gui::DebugMenuView::kValueShare;
    const auto fitsWhole = [&](const MenuItem& item, const std::string& where) {
        if (item.kind == ItemKind::Submenu || item.kind == ItemKind::Action) {
            return;
        }
        const std::string value = coney::debug::valueText(item);
        INFO(where << " = " << value);
        CHECK(coney::gui::fitText(painter, value, valueRoom) == value);
    };
    const auto world = session.model().openPage("World");
    REQUIRE(world != nullptr);
    for (const MenuItem& item : world->items()) {
        fitsWhole(item, item.label);
        if (item.kind == ItemKind::Submenu && item.label != "Lighting") {
            const std::shared_ptr<MenuPage> group = item.open();
            REQUIRE(group != nullptr);
            for (const MenuItem& child : group->items()) {
                fitsWhole(child, item.label + "/" + child.label);
            }
        }
    }

    // The developer overlay: every item of the page and of its groups.
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1280.0F, 720.0F);
    io.DeltaTime = 1.0F / 30.0F;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    ImGui::NewFrame();
    ImGui::Begin("World");
    coney::platform::ImGuiModelView overlay(session.model());
    const auto page = session.model().openPage("World");
    REQUIRE(page != nullptr);
    std::size_t drawn = 0;
    for (const MenuItem& item : page->items()) {
        CHECK(overlay.drawItem(item, "World/" + item.label));
        ++drawn;
        if (item.kind == ItemKind::Submenu) {
            // Hold the group's page: open() makes a fresh one (see the Lighting page's test for why).
            const std::shared_ptr<MenuPage> group = item.open();
            REQUIRE(group != nullptr);
            for (const MenuItem& child : group->items()) {
                INFO(item.label << "/" << child.label);
                CHECK_FALSE(child.label.empty());
                CHECK(overlay.drawItem(child, "World/" + item.label + "/" + child.label));
                ++drawn;
            }
        }
    }
    ImGui::End();
    ImGui::Render();
    ImGui::DestroyContext();
    CHECK(drawn > 30);
}
