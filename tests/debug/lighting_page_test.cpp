// SPDX-License-Identifier: GPL-3.0-or-later
// The Lighting page (src/debug/pages_lighting.cpp), the lighting lab (docs/guides/realtime-lighting.md#lab): its groups
// over a fake play mode with a synthetic level's lights and lighting frame, driven by a scripted pad through the input
// gate as a player drives the menu, drawn by both front ends from the one model, and *Dump this frame*'s files in the
// columns of the original's trace. No game, no disc.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <imgui.h>

#include "core/pad.h"
#include "debug/debug_session.h"
#include "debug/lighting_lab.h"
#include "debug/menu_model.h"
#include "debug/play_controls.h"
#include "graphics/level_lighting.h"
#include "graphics/light_manager.h"
#include "graphics/lighting_frame.h"
#include "gui/debug_menu_view.h"
#include "platform/imgui_model_view.h"
#include "support/fixtures.h"
#include "support/recording_device.h"

using coney::debug::DebugServices;
using coney::debug::DebugSession;
using coney::debug::ItemKind;
using coney::debug::MenuItem;
using coney::debug::MenuPage;
using coney::debug::TunableRegistry;
using coney::graphics::LightColour;
using coney::graphics::LightDescriptor;
using coney::graphics::LightHandle;
using coney::graphics::LightManager;
using coney::graphics::LightType;
using coney::graphics::SelectionCaller;
namespace pad = coney::pad;

namespace {

// A camera at the origin looking along +z (RenderWare's axes), draw distance 115.
coney::graphics::LightView camera() {
    return coney::graphics::LightView{.position = {}, .forward = {0.0F, 0.0F, 1.0F}, .nearClip = 0.5F, .farClip = 115};
}

// A level with an objects' ambient, a moonlight, a lamp in front of the camera and a corona-only light, culled for
// camera(), and a frame with two selections: a human near the lamp (game axes (0, -9, 1): RenderWare (0, 1, 9)) and a
// sector.
struct Level {
    Level() {
        LightDescriptor ambient;
        ambient.type = LightType::Ambient;
        ambient.colour = LightColour{0.1F, 0.1F, 0.1F, 1.0F};
        ambient.lights = coney::graphics::kLightsObjects;
        static_cast<void>(lighting.lights.addLight(ambient));
        LightDescriptor moon = ambient;
        moon.type = LightType::Directional;
        moon.direction = {0.0F, -1.0F, 0.0F};
        static_cast<void>(lighting.lights.addLight(moon));
        LightDescriptor lampDesc;
        lampDesc.type = LightType::Point;
        lampDesc.position = {0.0F, 1.0F, 10.0F};
        lampDesc.radius = 3.0F;
        lampDesc.colour = LightColour{1.0F, 0.9F, 0.8F, 1.0F};
        lampDesc.lights = coney::graphics::kLightsObjects | coney::graphics::kLightsWorld;
        lampDesc.corona = 0;
        lampDesc.coronaSize = 0.5F;
        lamp = lighting.lights.addLight(lampDesc);
        LightDescriptor glint = lampDesc;
        glint.radius = 0.0F;
        glint.position = {2.0F, 1.0F, 20.0F};
        glint.lights = coney::graphics::kLightsObjects;
        static_cast<void>(lighting.lights.addLight(glint));
        lighting.lights.beginViewport(camera(), 0);
        frame = coney::graphics::buildLightingFrame(lighting, camera());
        frame.coronas = lighting.lights.coronas().size();
        const coney::graphics::LightSphere body{.centre = {0.0F, 1.0F, 9.0F}, .radius = 1.0F};
        const auto chosen = lighting.lights.select(81.0F, body, true, true, false);
        frame.selections.push_back(coney::graphics::frameSelectionOf(lighting.lights, SelectionCaller::Human, 81.0F,
                                                                     body, true, true, false, true, chosen));
        const coney::graphics::LightSphere sector{.centre = {0.0F, 0.0F, 12.0F}, .radius = 20.0F};
        const auto lit = lighting.lights.select(0.0F, sector, false, true, false);
        frame.selections.push_back(coney::graphics::frameSelectionOf(lighting.lights, SelectionCaller::Sector, 0.0F,
                                                                     sector, false, true, false, false, lit));
    }

    coney::graphics::LevelLighting lighting;
    coney::graphics::LightingFrame frame;
    LightHandle lamp = 0;
};

// A play mode that offers the level's frame and lights; everything else is a dummy.
class FakePlay final : public coney::debug::PlayControls {
  public:
    explicit FakePlay(Level& level) : m_level(level) {}
    [[nodiscard]] std::string sceneName() const override { return "lit test"; }
    [[nodiscard]] coney::anim::Vec3 playerFeet() const override { return {0.0F, -9.0F, 0.0F}; }
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
    [[nodiscard]] const coney::graphics::LightingFrame* lightingFrame() const override { return &m_level.frame; }
    [[nodiscard]] LightManager* lightManager() override { return &m_level.lighting.lights; }

  private:
    Level& m_level;
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
    void text(std::string_view text, float /*x*/, float /*y*/, coney::graphics::Rgba /*colour*/) override {
        texts.emplace_back(text);
    }
    void flush(coney::graphics::RenderDevice& /*device*/) override {}

    // Whether some text drawn holds `wanted`.
    [[nodiscard]] bool drew(std::string_view wanted) const {
        for (const std::string& t : texts) {
            if (t.find(wanted) != std::string::npos) {
                return true;
            }
        }
        return false;
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

// The whole text of the file at `path`.
std::string readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// The lines of `text`.
std::vector<std::string> linesOf(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        lines.push_back(line);
    }
    return lines;
}

// The comma-separated fields of `line`, an empty last one included.
std::vector<std::string> fieldsOf(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    for (std::size_t comma = line.find(','); comma != std::string::npos; comma = line.find(',', start)) {
        fields.push_back(line.substr(start, comma - start));
        start = comma + 1;
    }
    fields.push_back(line.substr(start));
    return fields;
}

} // namespace

TEST_CASE("the Lighting page has the lab's groups and says what waits on other work", "[debug][lighting_lab]") {
    // Expected: unverified, #1250 (Coney's own debug menu: the groups of docs/guides/realtime-lighting.md#lab)
    TunableRegistry tunables;
    Level level;
    FakePlay play(level);
    DebugServices services;
    services.play = [&play]() -> coney::debug::PlayControls* { return &play; };
    DebugSession session(tunables, services, nullptr);
    const auto titles = session.model().pageTitles();
    REQUIRE(std::ranges::find(titles, "Lighting") != titles.end());
    const auto page = session.model().openPage("Lighting");
    REQUIRE(page != nullptr);
    for (const std::string_view group : {"Frame", "Counts", "Lights", "Selection", "Layers", "Debug draw"}) {
        INFO(group);
        CHECK(itemOn(*page, group).kind == ItemKind::Submenu);
    }
    CHECK(coney::debug::valueText(itemOn(*page, "Shadows")).find("not built yet") != std::string::npos);
    CHECK(coney::debug::valueText(itemOn(*page, "Original's trace")).find("not built yet") != std::string::npos);

    // Counts: the records in use (the built-in three and the level's four), the visible point lights, the coronas,
    // the atomics lit and the humans by their lights.
    const auto counts = open(*page, "Counts");
    CHECK(coney::debug::valueText(itemOn(*counts, "Light records")) == "7 of 512");
    CHECK(coney::debug::valueText(itemOn(*counts, "Point lights")) == "1 (world 1)");
    CHECK(coney::debug::valueText(itemOn(*counts, "Coronas")) == std::to_string(level.frame.coronas));
    CHECK(coney::debug::valueText(itemOn(*counts, "Atomics lit")) == "2");
    CHECK(coney::debug::valueText(itemOn(*counts, "Humans by lights")) == "3: 1, 4: 0, 5: 0, 6: 0");
    CHECK(coney::debug::valueText(itemOn(*counts, "Full lists")) == "0");

    // Lights: one line per record in use, with its source.
    const auto lights = open(*page, "Lights");
    CHECK(itemOn(*lights, "#0 ambient").detail == "built in (A)");
    CHECK(itemOn(*lights, "#2 point").detail == "built in (glow)");
    const std::string lampLabel = "#" + std::to_string(level.lamp - 1) + " point";
    CHECK(itemOn(*lights, lampLabel).detail == "script or light task");
    CHECK(lights->find("#6 corona only") != nullptr);

    // Selection: the player's atomic (no entity inspected), its lights in order.
    const auto selection = open(*page, "Selection");
    CHECK(coney::debug::valueText(itemOn(*selection, "Atomic")).starts_with("human"));
    const std::vector<std::string> lines = itemOn(*selection, "Lights").lines();
    REQUIRE(lines.size() == 3);
    CHECK(lines[0].find("ambient") != std::string::npos);
    CHECK(lines[1].find("directional") != std::string::npos);
    CHECK(lines[2].find("point") != std::string::npos);

    // Layers: the switches, and the views that wait on Coney's shaders.
    const auto layers = open(*page, "Layers");
    itemOn(*layers, "Coronas").setBool(false);
    CHECK_FALSE(session.lighting().layers.coronas);
    itemOn(*layers, "View").setChoice(1);
    CHECK(session.lighting().view == coney::graphics::LightingView::PrelightOnly);
    CHECK(coney::debug::valueText(itemOn(*layers, "Not built yet")).find("renderer item 22") != std::string::npos);

    // Without a play mode the groups that read the frame say so.
    DebugSession bare(tunables, DebugServices{}, nullptr);
    const auto none = open(*bare.model().openPage("Lighting"), "Counts");
    CHECK(none->find("No level") != nullptr);
}

TEST_CASE("a scripted pad holds the lighting, lights from the view camera, edits a light and dumps the frame",
          "[debug][lighting_lab]") {
    // Expected: unverified, #1250 (Coney's own debug menu, driven as docs/guides/debug-menu.md#controls says)
    TunableRegistry tunables;
    Level level;
    FakePlay play(level);
    FakeInput input;
    const coney::test::TempDir folder;
    DebugServices services;
    services.play = [&play]() -> coney::debug::PlayControls* { return &play; };
    services.runLogFolder = folder.path();
    DebugSession session(tunables, services, &input);
    std::uint64_t frame = 0;
    // One frame through the gate with port 1 holding `buttons`, then one with nothing held, so the next press is new.
    const auto press = [&](std::uint16_t buttons) {
        input.next[0].connected = true;
        input.next[0].buttons = buttons;
        static_cast<void>(session.gate().sample(frame++));
        input.next[0].buttons = 0;
        static_cast<void>(session.gate().sample(frame++));
    };
    const auto pressTimes = [&](std::uint16_t buttons, std::ptrdiff_t times) {
        for (std::ptrdiff_t i = 0; i < times; ++i) {
            press(buttons);
        }
    };

    // L3 + R3 opens the menu; down to Lighting (after Favourites), cross opens it, cross opens Frame.
    press(pad::kL3 | pad::kR3);
    REQUIRE(session.navigator().isOpen());
    const auto titles = session.model().pageTitles();
    pressTimes(pad::kDown, std::ranges::find(titles, "Lighting") - titles.begin() + 1);
    press(pad::kCross);
    REQUIRE(session.navigator().page() != nullptr);
    CHECK(session.navigator().page()->title() == "Lighting");
    press(pad::kCross);
    CHECK(session.navigator().page()->title() == "Frame");

    // Hold lighting, then Light from: the view camera; both on the drawing side only.
    CHECK_FALSE(session.lighting().hold);
    press(pad::kCross);
    CHECK(session.lighting().hold);
    press(pad::kDown);
    CHECK(session.lighting().lightFromGameCamera);
    press(pad::kRight);
    CHECK_FALSE(session.lighting().lightFromGameCamera);

    // Dump this frame: the frame's lights and selections in the run log's folder.
    press(pad::kDown);
    press(pad::kCross);
    const std::filesystem::path selections = folder.path() / "lighting-1-selections.csv";
    const std::filesystem::path lights = folder.path() / "lighting-1-lights.csv";
    REQUIRE(std::filesystem::exists(selections));
    REQUIRE(std::filesystem::exists(lights));
    CHECK(linesOf(readFile(selections)).size() == 1 + level.frame.selections.size());
    CHECK(linesOf(readFile(lights)).size() == 1 + level.frame.lights.size());
    CHECK(session.log().last(1).front().find("lighting-1-selections.csv") != std::string::npos);

    // Back to Lighting, down to Lights, into the lamp, and switch it off: the edit lands between two steps.
    press(pad::kCircle);
    CHECK(session.navigator().page()->title() == "Lighting");
    press(pad::kDown);
    press(pad::kDown);
    press(pad::kCross);
    CHECK(session.navigator().page()->title() == "Lights");
    // The records in use in order: 0, 1, 2 built in, then the level's 3, 4, the lamp and the corona-only light.
    pressTimes(pad::kDown, static_cast<std::ptrdiff_t>(level.lamp - 1));
    press(pad::kCross);
    CHECK(session.navigator().page()->title().starts_with("#" + std::to_string(level.lamp - 1)));
    REQUIRE(level.lighting.lights.light(level.lamp)->desc.on);
    // The light's line, then On.
    press(pad::kDown);
    press(pad::kCross);
    press(0);
    CHECK_FALSE(level.lighting.lights.light(level.lamp)->desc.on);
    CHECK(session.lightEdits().pending() == 0);
}

TEST_CASE("both debug-menu front ends draw the Lighting page from the one model", "[debug][lighting_lab]") {
    // Expected: docs/guides/debug-menu.md#adding-a-feature
    TunableRegistry tunables;
    Level level;
    FakePlay play(level);
    DebugServices services;
    services.play = [&play]() -> coney::debug::PlayControls* { return &play; };
    DebugSession session(tunables, services, nullptr);

    // The pad menu, opened on Lighting/Counts.
    auto& nav = session.navigator();
    nav.open();
    const auto titles = session.model().pageTitles();
    const auto index = std::ranges::find(titles, "Lighting") - titles.begin() + 1;
    for (std::ptrdiff_t i = 0; i < index; ++i) {
        nav.apply(coney::debug::MenuInputFrame{.action = coney::debug::MenuAction::Down});
    }
    nav.apply(coney::debug::MenuInputFrame{.action = coney::debug::MenuAction::Accept});
    nav.apply(coney::debug::MenuInputFrame{.action = coney::debug::MenuAction::Down});
    nav.apply(coney::debug::MenuInputFrame{.action = coney::debug::MenuAction::Accept});
    REQUIRE(nav.page() != nullptr);
    CHECK(nav.page()->title() == "Counts");
    RecordingPainter painter;
    coney::test::RecordingDevice device;
    coney::gui::DebugMenuView view;
    view.draw(session, painter, device);
    CHECK(painter.drew("Debug > Lighting > Counts"));
    CHECK(painter.drew("7 of 512"));

    // The developer overlay: every item of the page and of its groups.
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1280.0F, 720.0F);
    io.DeltaTime = 1.0F / 30.0F;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    ImGui::NewFrame();
    ImGui::Begin("Lighting");
    coney::platform::ImGuiModelView overlay(session.model());
    const auto page = session.model().openPage("Lighting");
    REQUIRE(page != nullptr);
    std::size_t drawn = 0;
    for (const MenuItem& item : page->items()) {
        CHECK(overlay.drawItem(item, "Lighting/" + item.label));
        ++drawn;
        if (item.kind == ItemKind::Submenu) {
            for (const MenuItem& child : item.open()->items()) {
                INFO(item.label << "/" << child.label);
                CHECK(overlay.drawItem(child, "Lighting/" + item.label + "/" + child.label));
                ++drawn;
            }
        }
    }
    ImGui::End();
    ImGui::Render();
    ImGui::DestroyContext();
    CHECK(drawn > 30);
}

TEST_CASE("a dumped frame's selections are in the original trace's columns, numbers only", "[debug][lighting_lab]") {
    // Expected: lighting.md#select, confirmed (code): per call the caller's return address, distSq, the sphere, flags,
    // pointLights, pulse, human, the count, then each chosen RpLight's type (+0x01), radius, colour and position
    Level level;
    const std::vector<std::string> columns = coney::debug::selectionColumns();
    REQUIRE(columns.size() == 11 + (8 * 9));
    CHECK(columns[0] == "caller");
    CHECK(columns[1] == "dist_sq");
    CHECK(columns[5] == "sphere_radius");
    CHECK(columns[6] == "flags");
    CHECK(columns[10] == "count");
    CHECK(columns[11] == "light0_type");
    CHECK(columns.back() == "light7_z");

    const std::vector<std::string> rows = linesOf(coney::debug::selectionsCsv(level.frame));
    REQUIRE(rows.size() == 3);
    CHECK(fieldsOf(rows[0]) == columns);
    const std::vector<std::string> human = fieldsOf(rows[1]);
    REQUIRE(human.size() == columns.size());
    CHECK(human[0] == "0x001746f8"); // HumanRender_Draw's return address
    CHECK(human[1] == "81");
    CHECK(human[6] == "1"); // objects
    CHECK(human[7] == "1"); // point lights
    CHECK(human[8] == "0"); // no pulse
    CHECK(human[9] == "1"); // a human
    CHECK(human[10] == "3");
    CHECK(human[11] == "2");   // ambient, RenderWare type 2
    CHECK(human[20] == "1");   // directional, type 1
    CHECK(human[29] == "128"); // point, 0x80
    CHECK(human[30] == "3");   // its radius
    CHECK(human[38].empty());  // no fourth light
    const std::vector<std::string> sector = fieldsOf(rows[2]);
    CHECK(sector[0] == "0x00411aa8"); // World_RenderSectorAtomic's return address
    CHECK(sector[6] == "2");          // the world
    CHECK(sector[9] == "0");

    // Numbers only: every field is empty or reads as a number.
    for (std::size_t r = 1; r < rows.size(); ++r) {
        for (const std::string& field : fieldsOf(rows[r])) {
            INFO(field);
            CHECK((field.empty() || coney::debug::parseNumber(field).has_value() || field.starts_with("0x")));
        }
    }
    const std::vector<std::string> lights = linesOf(coney::debug::frameLightsCsv(level.frame));
    REQUIRE(lights.size() == 1 + level.frame.lights.size());
    for (std::size_t r = 1; r < lights.size(); ++r) {
        for (const std::string& field : fieldsOf(lights[r])) {
            INFO(field);
            CHECK(coney::debug::parseNumber(field).has_value());
        }
    }
}

TEST_CASE("the lab's debug draw puts each light, the cull's reach and the selection into the scene in game axes",
          "[debug][lighting_lab]") {
    // Expected: lighting.md#cull, confirmed (code): the cull keeps point lights within three quarters of the draw
    // distance; positions are RenderWare's (x, z, -y) of the game's
    Level level;
    coney::debug::LightingDrawOptions options;
    CHECK(coney::debug::lightingLines(level.frame, options, nullptr).empty());

    options.pointLights = true;
    const coney::debug::DebugLines spheres = coney::debug::lightingLines(level.frame, options, nullptr);
    REQUIRE_FALSE(spheres.empty());
    // The lamp at RenderWare (0, 1, 10) is at game (0, -10, 1): its sphere's lines stay within 3 m of it.
    for (const coney::debug::DebugLine& line : spheres.lines()) {
        const float dx = line.a.x;
        const float dy = line.a.y + 10.0F;
        const float dz = line.a.z - 1.0F;
        CHECK((dx * dx) + (dy * dy) + (dz * dz) <= 9.01F);
    }

    options = coney::debug::LightingDrawOptions{};
    options.cullReach = true;
    const coney::debug::DebugLines reach = coney::debug::lightingLines(level.frame, options, nullptr);
    REQUIRE_FALSE(reach.empty());
    const coney::anim::Vec3 rim = reach.lines()[0].a;
    const float r = std::sqrt((rim.x * rim.x) + (rim.y * rim.y));
    CHECK(r > 86.0F); // 115 × 0.75 = 86.25 round the light view
    CHECK(r < 86.5F);

    options = coney::debug::LightingDrawOptions{};
    options.selection = true;
    const coney::graphics::FrameSelection* human =
        coney::debug::selectionNear(level.frame, SelectionCaller::Human, coney::anim::Vec3{0.0F, -9.0F, 0.0F});
    REQUIRE(human != nullptr);
    CHECK(human->caller == SelectionCaller::Human);
    const coney::debug::DebugLines chosen = coney::debug::lightingLines(level.frame, options, human);
    // A sphere of 3 great circles, then one line to each point light chosen.
    REQUIRE(chosen.size() == (3 * coney::debug::DebugLines::kCircleSegments) + 1);
    const coney::debug::DebugLine& toLamp = chosen.lines().back();
    CHECK(toLamp.b.y == -10.0F);
    CHECK(toLamp.b.z == 1.0F);
}

TEST_CASE("two colour edits of one light in a frame both land between the steps", "[debug][lighting_lab]") {
    // Expected: unverified, #1250 (Coney's own debug menu: edits queued and applied between two steps)
    TunableRegistry tunables;
    Level level;
    FakePlay play(level);
    DebugServices services;
    services.play = [&play]() -> coney::debug::PlayControls* { return &play; };
    DebugSession session(tunables, services, nullptr);
    const std::string label = "#" + std::to_string(level.lamp - 1) + " point";
    const auto lamp = open(*open(*session.model().openPage("Lighting"), "Lights"), label);
    itemOn(*lamp, "Red").setNumber(0.25);
    itemOn(*lamp, "Green").setNumber(0.5);
    CHECK(session.lightEdits().pending() == 2);
    static_cast<void>(session.lightEdits().applyPending(&level.lighting.lights));
    const LightColour colour = level.lighting.lights.light(level.lamp)->desc.colour;
    CHECK(colour.r == 0.25F);
    CHECK(colour.g == 0.5F);
    CHECK(colour.b == 0.8F);
}
