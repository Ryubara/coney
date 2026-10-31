// SPDX-License-Identifier: GPL-3.0-or-later
// Coney Studio's interface over a bare Dear ImGui context (no window, no OpenGL), as the developer overlay's tests run:
// the default docked layout's panels, New and Open project, a level opened, a selection the Hierarchy and the Inspector
// both show, and Play as Run in Coney (docs/guides/studio/prototype.md).
#include "studio/studio_app.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <imgui.h>
#include <imgui_internal.h> // FindWindowByName

#include "studio/inspector.h"
#include "studio/run_in_coney.h"
#include "studio/theme.h"
#include "support/fixtures.h"
#include "support/test_paths.h"

using Catch::Approx;
using coney::studio::ConsoleLine;
using coney::studio::StudioApp;
using coney::studio::StudioSettings;

namespace {

// A host that opens no dialog and starts no process: it records what it was asked and answers the folder picker with
// a chosen folder.
class FakeHost final : public coney::studio::StudioHost {
  public:
    void pickFolder(std::string_view, const std::optional<std::filesystem::path>& startIn,
                    std::function<void(std::optional<std::filesystem::path>)> done) override {
        pickStarts.push_back(startIn);
        done(folder);
    }
    coney::studio::UserFolders userFolders() override { return folders; }
    std::expected<void, coney::Error> startProcess(const std::vector<std::string>& command) override {
        started.push_back(command);
        return {};
    }
    std::uint64_t drawLevel(const coney::studio::LevelDocument&, const coney::studio::LevelView& view) override {
        lastWidth = view.width;
        ++draws;
        return 0;
    }
    bool setRelativeMouse(bool on) override {
        relativeAsks.push_back(on);
        return relativeSupported;
    }
    coney::studio::MouseMotion takeRelativeMotion() override { return std::exchange(motion, {}); }

    std::optional<std::filesystem::path> folder;
    coney::studio::UserFolders folders;                           // what userFolders() reports
    std::vector<std::optional<std::filesystem::path>> pickStarts; // where each folder picker was asked to start
    std::vector<std::vector<std::string>> started;
    float lastWidth = 0.0F;
    int draws = 0;
    bool relativeSupported = true;     // what setRelativeMouse() answers
    std::vector<bool> relativeAsks;    // every setRelativeMouse() call's argument, in order
    coney::studio::MouseMotion motion; // the movement the next takeRelativeMotion() hands over
};

// A bare Dear ImGui context with docking, sized like a 1600 x 900 window, destroyed at the end.
class BareContext {
  public:
    BareContext() {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(1600.0F, 900.0F);
        io.DeltaTime = 1.0F / 30.0F;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    }
    ~BareContext() { ImGui::DestroyContext(); }
    BareContext(const BareContext&) = delete;
    BareContext& operator=(const BareContext&) = delete;
    BareContext(BareContext&&) = delete;
    BareContext& operator=(BareContext&&) = delete;

    // Runs `frames` frames of `app`, one camera step each.
    static void run(StudioApp& app, int frames) {
        for (int i = 0; i < frames; ++i) {
            ImGui::NewFrame();
            app.frame(1, 1.0F / 30.0F, 1.0F);
            ImGui::Render();
        }
    }
};

// Settings that find Coney's own assets in the checkout and name a `coney` that need not exist.
StudioSettings testSettings() {
    StudioSettings settings;
    settings.assets = coney::test::assetsDir();
    settings.coney = "coney-under-test";
    return settings;
}

// The middle of the Level view's panel, where a mouse press flies the camera.
ImVec2 levelCentre() {
    const ImGuiWindow* window = ImGui::FindWindowByName(coney::studio::panels::kLevel);
    REQUIRE(window != nullptr);
    return ImVec2(window->Pos.x + (window->Size.x * 0.5F), window->Pos.y + (window->Size.y * 0.5F));
}

// Moves the mouse to `at` and presses (or releases) the right button, with a frame in between so Dear ImGui takes the
// move and the button on separate frames as it does for a real mouse.
void rightButton(StudioApp& app, ImVec2 at, bool down) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(at.x, at.y);
    BareContext::run(app, 1);
    io.AddMouseButtonEvent(ImGuiMouseButton_Right, down);
    BareContext::run(app, 1);
}

} // namespace

TEST_CASE("Studio opens with the default layout's six panels docked in one dock space", "[studio]") {
    const BareContext context;
    FakeHost host;
    StudioApp app(host, testSettings());
    BareContext::run(app, 3);
    // Expected: the default layout's panels (docs/guides/studio/editor-experience.md#layout-default).
    for (const char* name :
         {coney::studio::panels::kHierarchy, coney::studio::panels::kLevel, coney::studio::panels::kGame,
          coney::studio::panels::kInspector, coney::studio::panels::kFileSystem, coney::studio::panels::kConsole}) {
        INFO(name);
        const ImGuiWindow* window = ImGui::FindWindowByName(name);
        REQUIRE(window != nullptr);
        CHECK(window->DockId != 0);
    }
    // Godot's arrangement: the Hierarchy left of the Level view, the Inspector right of the Game view.
    CHECK(ImGui::FindWindowByName("Hierarchy")->Pos.x < ImGui::FindWindowByName("Level")->Pos.x);
    CHECK(ImGui::FindWindowByName("Level")->Pos.x < ImGui::FindWindowByName("Game")->Pos.x);
    CHECK(ImGui::FindWindowByName("Game")->Pos.x < ImGui::FindWindowByName("Inspector")->Pos.x);
    CHECK(ImGui::FindWindowByName("File system")->Pos.y > ImGui::FindWindowByName("Hierarchy")->Pos.y);
    CHECK(app.windowTitle() == "Coney Studio");
}

TEST_CASE("a new project opens with its level in every panel and the camera at its first view", "[studio]") {
    // Expected: docs/guides/studio/prototype.md#features, what the prototype does (design).
    const BareContext context;
    const coney::test::TempDir dir;
    FakeHost host;
    StudioApp app(host, testSettings());
    REQUIRE(app.newProject(dir.path() / "yard", "yard", "combat").has_value());
    REQUIRE(app.project().has_value());
    REQUIRE(app.level() != nullptr);
    // The shipped course's textures came with it, so Coney can draw it from the project.
    CHECK(std::filesystem::is_regular_file(dir.path() / "yard" / "textures" / "dark_grid.png"));
    CHECK(std::filesystem::is_regular_file(dir.path() / "yard" / "textures" / "License.txt"));
    const auto& view = app.level()->layout().views.front();
    CHECK(app.camera().position().x == view.position.x);
    CHECK(app.windowTitle() == "Coney Studio - yard - main.layout");
    REQUIRE(app.goToView("overview")); // the course's second view (assets/sandbox/combat.layout)
    CHECK(app.camera().position().z == app.level()->layout().views.at(1).position.z);
    CHECK_FALSE(app.goToView("no-such-view"));
    BareContext::run(app, 2);
    CHECK(host.draws == 2);
    CHECK(host.lastWidth > 100.0F);
    REQUIRE(app.inspector() != nullptr);
    CHECK(app.inspector()->find("Lighting") != nullptr); // nothing selected: the level's settings
    // Without Coney's assets there is no texture to give the project: it refuses, and makes nothing.
    StudioSettings bare = testSettings();
    bare.assets = dir.path() / "no-assets";
    StudioApp lost(host, bare);
    const auto refused = lost.newProject(dir.path() / "lost", "lost", "basic");
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().code == coney::ErrorCode::NotFound);
    CHECK_FALSE(std::filesystem::exists(dir.path() / "lost"));
}

TEST_CASE("a selection is shared by the Hierarchy and the Inspector", "[studio]") {
    // Expected: docs/guides/studio/prototype.md#features, what the prototype does (design).
    const BareContext context;
    const coney::test::TempDir dir;
    FakeHost host;
    StudioApp app(host, testSettings());
    REQUIRE(app.newProject(dir.path() / "b", "b", "basic").has_value());
    // The Basic level: the ground (0), the spawn point (1), the camera view (2).
    app.select(1);
    CHECK(app.selection() == 1U);
    REQUIRE(app.inspector() != nullptr);
    const auto* identity = app.inspector()->find("Identity");
    REQUIRE(identity != nullptr);
    BareContext::run(app, 2);
    // F frames it: the glide starts at the Level view's next draw and ends within its half second of steps.
    const auto before = app.camera().position();
    app.frameSelection();
    BareContext::run(app, 20);
    CHECK_FALSE(app.camera().gliding());
    CHECK(app.camera().position().x == app.camera().glideTarget().x);
    CHECK(app.camera().position().y != before.y);
    app.select(99);
    CHECK_FALSE(app.selection().has_value());
    app.select(std::nullopt);
    CHECK(app.inspector()->find("Lighting") != nullptr);
}

TEST_CASE("Open project opens an existing project and refuses a folder that is not one", "[studio]") {
    // Expected: docs/guides/studio/prototype.md#features, what the prototype does (design).
    const BareContext context;
    const coney::test::TempDir dir;
    FakeHost host;
    {
        StudioApp maker(host, testSettings());
        REQUIRE(maker.newProject(dir.path() / "p", "p", "basic").has_value());
    }
    StudioApp app(host, testSettings());
    CHECK(app.openProject(dir.path()).error().code == coney::ErrorCode::NotFound);
    REQUIRE(app.openProject(dir.path() / "p").has_value());
    const auto& project = app.project();
    REQUIRE(project.has_value());
    if (!project) {
        return; // REQUIRE has failed the test; this keeps clang-tidy's optional check satisfied
    }
    CHECK(project->manifest().name == "p");
    REQUIRE(app.level() != nullptr);
    CHECK(app.level()->entities().size() == 3);
    CHECK(app.openLevel("project:/../outside.layout").error().code == coney::ErrorCode::InvalidArgument);
}

TEST_CASE("Play starts Coney on the open level as a child process", "[studio]") {
    // Expected: docs/guides/studio/prototype.md#features, what the prototype does (design).
    const BareContext context;
    const coney::test::TempDir dir;
    FakeHost host;
    StudioApp app(host, testSettings());
    CHECK(app.runInConey().error().code == coney::ErrorCode::InvalidArgument); // no level yet
    REQUIRE(app.newProject(dir.path() / "p", "p", "basic").has_value());
    REQUIRE(app.runInConey().has_value());
    REQUIRE(host.started.size() == 1);
    const auto& project = app.project();
    REQUIRE(project.has_value());
    if (!project) {
        return;
    }
    const auto& command = host.started.front();
    CHECK(command.front() == "coney-under-test");
    CHECK(command.at(1) == "--sandbox"); // no disc: the level is flown round
    CHECK(std::filesystem::path(command.at(2)) == project->root() / "levels" / "main.layout");
}

TEST_CASE("Run in Coney plays the level from its spawn with a disc and flies it without one", "[studio]") {
    coney::studio::RunRequest request{.coney = "bin/coney", .level = "p/levels/main.layout"};
    CHECK(coney::studio::runInConeyCommand(request) ==
          std::vector<std::string>{"bin/coney", "--sandbox", "p/levels/main.layout"});
    request.disc = "H:/";
    request.spawn = "start";
    request.assets = "share/assets";
    // Expected: `--play-level sandbox:NAME --spawn NAME` (docs/guides/studio/editor-experience.md#project-coney).
    CHECK(coney::studio::runInConeyCommand(request) ==
          std::vector<std::string>{"bin/coney", "--disc", "H:/", "--play-level", "sandbox:p/levels/main.layout",
                                   "--spawn", "start", "--assets", "share/assets"});
    CHECK(coney::studio::coneyBeside("build/coney-studio.exe") == std::filesystem::path("build/coney.exe"));
    CHECK(coney::studio::coneyBeside("build/coney-studio") == std::filesystem::path("build/coney"));
}

TEST_CASE("the theme derives Dear ImGui's colours from Godot's base and accent", "[studio]") {
    const BareContext context;
    const coney::studio::ThemeTokens tokens;
    // Expected: docs/guides/studio/editor-experience.md#theme-defaults, the design: Godot 4's Modern Default preset,
    // base #292929 and accent #569EFF.
    CHECK(tokens.base == coney::studio::hexColour(0x292929));
    CHECK(tokens.accent == coney::studio::hexColour(0x569EFF));
    ImGuiStyle style;
    coney::studio::applyTheme(tokens, 2.0F, style);
    const ImVec4 window = style.Colors[ImGuiCol_WindowBg];
    CHECK(window.x == tokens.base.r);
    const ImVec4 check = style.Colors[ImGuiCol_CheckMark];
    CHECK(check.z == tokens.accent.b);
    CHECK(style.Colors[ImGuiCol_Text].x == 1.0F); // white text on the dark base
    CHECK(style.FrameRounding == 8.0F);           // 4 px at UI scale 2
    CHECK(style.Colors[ImGuiCol_FrameBg].x < window.x);
    CHECK(coney::studio::textColourFor(coney::studio::hexColour(0xF0F0F0)).r == 0.0F);
}

TEST_CASE("the theme paints Godot's dark surfaces round the panels and the accent on what is chosen", "[studio]") {
    const BareContext context;
    const coney::studio::ThemeTokens tokens;
    // Expected: docs/guides/studio/editor-experience.md#theme-imgui, Godot's derivation (design): its dark colours are
    // the base moved towards black by the contrast, 1.5 and 2 times it; the default 0.3 keeps 0.7, 0.55 and 0.4 of
    // each channel of #292929.
    const coney::studio::ThemePalette palette = coney::studio::derivePalette(tokens);
    CHECK(palette.base == tokens.base);
    CHECK(palette.dark1 == coney::studio::shade(tokens.base, tokens.contrast));
    CHECK(palette.dark2 == coney::studio::shade(tokens.base, tokens.contrast * 1.5F));
    CHECK(palette.dark3 == coney::studio::shade(tokens.base, tokens.contrast * 2.0F));
    CHECK(palette.selection.r == tokens.accent.r);
    CHECK(palette.selection.a > palette.section.a);
    CHECK(palette.section.a > 0.0F);

    ImGuiStyle style;
    coney::studio::applyTheme(tokens, 2.0F, style);
    const auto same = [](const ImVec4& imgui, coney::studio::ThemeColour ours) {
        return imgui.x == ours.r && imgui.y == ours.g && imgui.z == ours.b && imgui.w == ours.a;
    };
    // Behind and between the panels: the tab strips, the menu bar and the dock's gaps are the darker shade, the gaps
    // one base spacing wide.
    CHECK(same(style.Colors[ImGuiCol_TitleBg], palette.dark2));
    CHECK(same(style.Colors[ImGuiCol_MenuBarBg], palette.dark2));
    CHECK(same(style.Colors[ImGuiCol_DockingEmptyBg], palette.dark2));
    CHECK(style.DockingSeparatorSize == 8.0F); // 4 px at UI scale 2
    // The active tab is the panel's own colour with the accent's line over it.
    CHECK(same(style.Colors[ImGuiCol_TabSelected], palette.base));
    CHECK(same(style.Colors[ImGuiCol_TabSelectedOverline], palette.accent));
    CHECK(style.TabBarOverlineSize >= 2.0F);
    // Fields are the first dark shade; a chosen row, a toggle and the focus are the accent.
    CHECK(same(style.Colors[ImGuiCol_FrameBg], palette.dark1));
    CHECK(same(style.Colors[ImGuiCol_Header], palette.selection));
    CHECK(same(style.Colors[ImGuiCol_CheckMark], palette.accent));
    CHECK(same(style.Colors[ImGuiCol_NavCursor], palette.accent));
    // The fonts are part of the tokens, so the user can change them with the colours.
    CHECK(tokens.interfaceFont == "Inter-Regular.ttf");
    CHECK(tokens.codeFont == "JetBrainsMono-Regular.ttf");
}

TEST_CASE("the Console keeps its newest lines", "[studio]") {
    // Expected: docs/guides/studio/prototype.md#features, what the prototype does (design).
    FakeHost host;
    StudioApp app(host, testSettings());
    for (std::size_t i = 0; i < StudioApp::kConsoleLines + 5; ++i) {
        app.log(ConsoleLine::Level::Info, std::to_string(i));
    }
    CHECK(app.console().size() == StudioApp::kConsoleLines);
    CHECK(app.console().back().text == std::to_string(StudioApp::kConsoleLines + 4));
}

TEST_CASE(
    "holding the right button over the Level view asks the host for relative mouse mode and releasing gives it back",
    "[studio]") {
    // Expected: docs/guides/studio/level-view.md#fly-look, the cursor is kept inside the window while looking and put
    // back on release (SDL's relative mouse mode, behind StudioHost).
    const BareContext context;
    const coney::test::TempDir dir;
    FakeHost host;
    StudioApp app(host, testSettings());
    REQUIRE(app.newProject(dir.path() / "p", "p", "basic").has_value());
    BareContext::run(app, 4);
    CHECK(host.relativeAsks.empty()); // nothing asked until the button is pressed
    const ImVec2 centre = levelCentre();
    rightButton(app, centre, true);
    REQUIRE(host.relativeAsks.size() == 1);
    CHECK(host.relativeAsks.front());
    // Held for several frames, it asks once, not every frame.
    BareContext::run(app, 5);
    CHECK(host.relativeAsks.size() == 1);
    rightButton(app, centre, false);
    REQUIRE(host.relativeAsks.size() == 2);
    CHECK_FALSE(host.relativeAsks.back());
    BareContext::run(app, 3);
    CHECK(host.relativeAsks.size() == 2);
    // A press outside the Level view does not fly.
    const ImGuiWindow* hierarchy = ImGui::FindWindowByName(coney::studio::panels::kHierarchy);
    REQUIRE(hierarchy != nullptr);
    rightButton(app, ImVec2(hierarchy->Pos.x + 20.0F, hierarchy->Pos.y + 60.0F), true);
    CHECK(host.relativeAsks.size() == 2);
}

TEST_CASE("the look follows the host's relative motion, scaled by the display and the sensitivity setting",
          "[studio]") {
    // Expected: docs/guides/studio/level-view.md#fly-look, the movement comes from the relative mode in physical
    // pixels and is counted in the display's scale-1 pixels; without relative mode Dear ImGui's own movement is used.
    const auto yawAfter = [](float scale, float sensitivity, bool relative, float pixels) {
        const BareContext context;
        const coney::test::TempDir dir;
        FakeHost host;
        host.relativeSupported = relative;
        StudioSettings settings = testSettings();
        settings.uiScale = scale;
        settings.lookSensitivity = sensitivity;
        StudioApp app(host, settings);
        REQUIRE(app.newProject(dir.path() / "p", "p", "basic").has_value());
        BareContext::run(app, 4);
        const ImVec2 centre = levelCentre();
        rightButton(app, centre, true);
        const float before = app.camera().yawDegrees();
        host.motion = {.x = pixels, .y = 0.0F};
        if (!relative) {
            ImGui::GetIO().AddMousePosEvent(centre.x + pixels, centre.y);
        }
        BareContext::run(app, 1);
        return app.camera().yawDegrees() - before;
    };
    constexpr float kDegreesPerRadian = 180.0F / 3.14159265F;
    // 100 pixels at the default turn 0.25 radians, to the right, which lowers the heading.
    CHECK(yawAfter(1.0F, 1.0F, true, 100.0F) == Approx(-100.0F * 0.0025F * kDegreesPerRadian));
    // On a display at 200 %, the same hand movement is twice the pixels and turns the same.
    CHECK(yawAfter(2.0F, 1.0F, true, 200.0F) == Approx(yawAfter(1.0F, 1.0F, true, 100.0F)));
    CHECK(yawAfter(1.0F, 2.0F, true, 100.0F) == Approx(2.0F * yawAfter(1.0F, 1.0F, true, 100.0F)));
    // A host without relative mode: Dear ImGui's mouse movement turns the camera the same way.
    CHECK(yawAfter(1.0F, 1.0F, false, 100.0F) == Approx(yawAfter(1.0F, 1.0F, true, 100.0F)));
}

namespace {

// Writes the manifest of a project called `name` into the folder `root`, enough for it to open (no level).
void makeProject(const std::filesystem::path& root, const std::string& name) {
    std::filesystem::create_directories(root);
    std::ofstream(root / "coney-project.toml") << "[project]\nname = \"" << name << "\"\n";
}

// A fake user whose documents folder is `<dir>/Users/ana/Documents` (home `<dir>/Users/ana`).
coney::studio::UserFolders fakeUser(const std::filesystem::path& dir) {
    return coney::studio::UserFolders{.documents = dir / "Users" / "ana" / "Documents", .home = dir / "Users" / "ana"};
}

// The folder `fakeUser()` has Studio keep projects in.
std::filesystem::path fakeProjects(const std::filesystem::path& dir) {
    return dir / "Users" / "ana" / "Documents" / "Coney Studio" / "Projects";
}

} // namespace

TEST_CASE("New project suggests the projects folder and makes it on first use", "[studio]") {
    // Expected: the issue's rule: `Coney Studio/Projects` in the documents folder, created on first use, suggested by
    // New project.
    const BareContext context;
    const coney::test::TempDir dir;
    FakeHost host;
    host.folders = fakeUser(dir.path());
    StudioApp app(host, testSettings());
    CHECK(app.projectsFolder() == fakeProjects(dir.path()));
    CHECK(app.defaultProjectsFolder() == fakeProjects(dir.path()));
    CHECK_FALSE(std::filesystem::exists(fakeProjects(dir.path()))); // asking where it is makes nothing
    app.askNewProject();
    CHECK(std::filesystem::is_directory(fakeProjects(dir.path())));
    CHECK(std::filesystem::path(app.newProjectFolder()) == fakeProjects(dir.path()));
    // The dialog's Create then makes the project inside it.
    BareContext::run(app, 2);
    REQUIRE(app.newProject(fakeProjects(dir.path()) / "yard", "yard", "basic").has_value());
    CHECK(std::filesystem::is_regular_file(fakeProjects(dir.path()) / "yard" / "coney-project.toml"));
}

TEST_CASE("Open project starts the folder picker in the projects folder", "[studio]") {
    const BareContext context;
    const coney::test::TempDir dir;
    FakeHost host;
    host.folders = fakeUser(dir.path());
    StudioApp app(host, testSettings());
    app.askOpenProject();
    REQUIRE(host.pickStarts.size() == 1);
    CHECK(host.pickStarts.front() == fakeProjects(dir.path()));
    CHECK(std::filesystem::is_directory(fakeProjects(dir.path()))); // made so the picker has somewhere to start
    // A user with no documents or home folder gets the system's choice, and New project has nothing to suggest.
    FakeHost bare;
    StudioApp lost(bare, testSettings());
    CHECK_FALSE(lost.projectsFolder().has_value());
    lost.askOpenProject();
    REQUIRE(bare.pickStarts.size() == 1);
    CHECK_FALSE(bare.pickStarts.front().has_value());
    lost.askNewProject();
    CHECK(lost.newProjectFolder().empty());
}

TEST_CASE("the home folder holds the projects folder when there are no documents", "[studio]") {
    const BareContext context;
    const coney::test::TempDir dir;
    FakeHost host;
    host.folders = coney::studio::UserFolders{.documents = std::nullopt, .home = dir.path() / "home" / "bo"};
    StudioApp app(host, testSettings());
    CHECK(app.projectsFolder() == dir.path() / "home" / "bo" / "Coney Studio" / "Projects");
}

TEST_CASE("the projects folder setting replaces the default, is saved with Studio's preferences and is read again",
          "[studio]") {
    const BareContext context;
    const coney::test::TempDir dir;
    FakeHost host;
    host.folders = fakeUser(dir.path());
    StudioSettings settings = testSettings();
    settings.preferencesFile = dir.path() / "data" / "coney-studio-preferences.toml";
    {
        StudioApp app(host, settings);
        CHECK(app.projectsFolder() == fakeProjects(dir.path()));
        REQUIRE(app.setProjectsFolder(dir.path() / "my work").has_value());
        CHECK(app.projectsFolder() == dir.path() / "my work");
        CHECK(std::filesystem::is_regular_file(settings.preferencesFile)); // saved at once, not at quit
    }
    {
        StudioApp next(host, settings);
        CHECK(next.projectsFolder() == dir.path() / "my work");
        next.askNewProject();
        CHECK(std::filesystem::path(next.newProjectFolder()) == dir.path() / "my work");
        CHECK(std::filesystem::is_directory(dir.path() / "my work"));
        // Putting the default back is a setting too.
        REQUIRE(next.setProjectsFolder(std::nullopt).has_value());
        CHECK(next.projectsFolder() == fakeProjects(dir.path()));
    }
    StudioApp last(host, settings);
    CHECK(last.projectsFolder() == fakeProjects(dir.path()));
    CHECK_FALSE(last.preferences().projectsFolder.has_value());
}

TEST_CASE("opened and made projects fill the recent list, most recent first, and it is kept for the next run",
          "[studio]") {
    const BareContext context;
    const coney::test::TempDir dir;
    FakeHost host;
    StudioSettings settings = testSettings();
    settings.preferencesFile = dir.path() / "prefs.toml";
    makeProject(dir.path() / "a", "a");
    makeProject(dir.path() / "b", "b");
    {
        StudioApp app(host, settings);
        REQUIRE(app.recentProjects().empty());
        REQUIRE(app.openProject(dir.path() / "a").has_value());
        REQUIRE(app.openProject(dir.path() / "b").has_value());
        REQUIRE(app.newProject(dir.path() / "c", "c", "basic").has_value());
        REQUIRE(app.recentProjects().size() == 3);
        CHECK(app.recentProjects()[0].name == "c");
        CHECK(app.recentProjects()[1].name == "b");
        CHECK(app.recentProjects()[2].name == "a");
        // Opening a again moves it up, and a failed open adds nothing.
        REQUIRE(app.openProject(dir.path() / "a").has_value());
        CHECK_FALSE(app.openProject(dir.path() / "nothing").has_value());
        REQUIRE(app.recentProjects().size() == 3);
        CHECK(app.recentProjects().front().name == "a");
        CHECK_FALSE(app.recentProjects().front().missing);
    }
    StudioApp next(host, settings);
    REQUIRE(next.recentProjects().size() == 3);
    CHECK(next.recentProjects()[0].name == "a");
    CHECK(next.recentProjects()[1].name == "c");
    CHECK(next.recentProjects()[2].name == "b");
}

TEST_CASE("the recent list keeps ten projects", "[studio]") {
    const BareContext context;
    const coney::test::TempDir dir;
    FakeHost host;
    StudioApp app(host, testSettings());
    for (int i = 0; i < 13; ++i) {
        const std::string name = "p" + std::to_string(i);
        makeProject(dir.path() / name, name);
        REQUIRE(app.openProject(dir.path() / name).has_value());
    }
    REQUIRE(app.recentProjects().size() == coney::studio::kMaxRecentProjects);
    CHECK(app.recentProjects().front().name == "p12");
    CHECK(app.recentProjects().back().name == "p3");
}

TEST_CASE("a recent project that is gone is marked missing, refuses to open and can be removed", "[studio]") {
    const BareContext context;
    const coney::test::TempDir dir;
    FakeHost host;
    StudioSettings settings = testSettings();
    settings.preferencesFile = dir.path() / "prefs.toml";
    makeProject(dir.path() / "kept", "kept");
    makeProject(dir.path() / "gone", "gone");
    StudioApp app(host, settings);
    REQUIRE(app.openProject(dir.path() / "gone").has_value());
    REQUIRE(app.openProject(dir.path() / "kept").has_value());
    std::filesystem::remove_all(dir.path() / "gone");
    app.refreshRecent();
    REQUIRE(app.recentProjects().size() == 2);
    CHECK_FALSE(app.recentProjects()[0].missing); // kept
    CHECK(app.recentProjects()[1].missing);       // gone, still listed so the user sees it went
    CHECK(app.recentProjects()[1].name == "gone");
    // Opening it says which folder is missing, and leaves the list and the open project as they were.
    const auto refused = app.openRecent(dir.path() / "gone");
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().code == coney::ErrorCode::NotFound);
    CHECK(refused.error().message.find("gone") != std::string::npos);
    CHECK(app.recentProjects().size() == 2);
    REQUIRE(app.project().has_value());
    CHECK(app.project()->manifest().name == "kept");
    // Remove takes it off the list, and the file, so it does not come back.
    CHECK(app.removeRecent(dir.path() / "gone"));
    CHECK_FALSE(app.removeRecent(dir.path() / "gone"));
    REQUIRE(app.recentProjects().size() == 1);
    CHECK(app.recentProjects().front().name == "kept");
    StudioApp next(host, settings);
    CHECK(next.recentProjects().size() == 1);
    // A recent project that is there opens by its entry.
    REQUIRE(next.openRecent(dir.path() / "kept").has_value());
    REQUIRE(next.project().has_value());
    CHECK(next.project()->manifest().name == "kept");
}

TEST_CASE("the start screen draws with recent projects, missing ones too, until a project opens", "[studio]") {
    // The panels read the list every frame: a frame with a present entry, a missing entry and no project must draw
    // without trouble, and the Level view then shows the level once a project opens.
    const BareContext context;
    const coney::test::TempDir dir;
    FakeHost host;
    StudioSettings settings = testSettings();
    settings.preferencesFile = dir.path() / "prefs.toml";
    makeProject(dir.path() / "here", "here");
    makeProject(dir.path() / "gone", "gone");
    {
        StudioApp earlier(host, settings);
        REQUIRE(earlier.openProject(dir.path() / "gone").has_value());
        REQUIRE(earlier.openProject(dir.path() / "here").has_value());
    }
    std::filesystem::remove_all(dir.path() / "gone");
    StudioApp app(host, settings);
    REQUIRE(app.recentProjects().size() == 2);
    CHECK(app.recentProjects()[1].missing);
    BareContext::run(app, 3);
    CHECK(host.draws == 0); // no level: nothing is drawn into the Level view
    REQUIRE(app.newProject(dir.path() / "made", "made", "basic").has_value());
    BareContext::run(app, 2);
    CHECK(host.draws == 2);
}
