// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <deque>
#include <expected>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.h"
#include "core/interpolation.h"
#include "debug/menu_model.h"
#include "studio/level_camera.h"
#include "studio/level_document.h"
#include "studio/project.h"
#include "studio/projects_folder.h"
#include "studio/studio_preferences.h"
#include "studio/theme.h"

struct ImFont;

namespace coney::platform {
class ImGuiModelView;
}

// Coney Studio's window contents: the menu bar, the toolbar, the default docked layout and its panels (Hierarchy,
// Level view, Game view, Inspector, File system, Console), and what they act on: the open project, the open level,
// the selection and the Level view's camera (docs/guides/studio/prototype.md). It draws with Dear ImGui and touches no
// window, graphics API or operating system itself: what needs one (a folder picker, drawing the level, starting a
// process) is the host's, which src/platform/ supplies and the tests replace. Coney's own; the original has no editor.

namespace coney::studio {

/// The Level view's vertical view window, half its height at distance 1: tan(22.5 degrees), a 45 degree field
/// (Coney's choice until Studio has a lens setting).
inline constexpr float kLevelHalfHeight = 0.4142F;

/// The panels' window names, which are also Dear ImGui's ids for them and the tests' handles.
namespace panels {
inline constexpr const char* kHierarchy = "Hierarchy";
inline constexpr const char* kLevel = "Level";
inline constexpr const char* kGame = "Game";
inline constexpr const char* kInspector = "Inspector";
inline constexpr const char* kFileSystem = "File system";
inline constexpr const char* kConsole = "Console";
} // namespace panels

/// Mouse movement in physical pixels: right and down positive.
struct MouseMotion {
    float x = 0.0F;
    float y = 0.0F;
};

/// What Studio asks of the platform.
class StudioHost {
  public:
    virtual ~StudioHost() = default;
    /// Asks the user for a folder, with the picker opened at `startIn` when that is given and exists, then calls `done`
    /// with it, or with nothing when they cancel. May call `done` later, from the event loop; never opens a dialog in
    /// a test.
    virtual void pickFolder(std::string_view title, const std::optional<std::filesystem::path>& startIn,
                            std::function<void(std::optional<std::filesystem::path>)> done) = 0;
    /// The user's documents and home folders, as the platform reports them (SDL3's `SDL_GetUserFolder`); either may be
    /// missing. The default projects folder is made from them (defaultProjectsFolder()).
    virtual UserFolders userFolders() = 0;
    /// Starts `command` (the program first) as a child process that runs on its own. Fails with
    /// ErrorCode::PlatformFailure when it cannot be started.
    virtual std::expected<void, Error> startProcess(const std::vector<std::string>& command) = 0;
    /// Draws `level` through `view` (its panel's size in pixels) and returns the texture holding the picture as Dear
    /// ImGui's texture id, or 0 when nothing could be drawn (no graphics, as in the tests).
    virtual std::uint64_t drawLevel(const LevelDocument& level, const LevelView& view) = 0;
    /// Asks the window to keep the mouse cursor inside it, hidden, and to report the mouse's movement through
    /// takeRelativeMotion() (SDL's relative mouse mode), or with `on` false to give the cursor back where it was when
    /// it was taken. Returns whether the host now reports the movement; a host that cannot returns false and the
    /// interface goes on with Dear ImGui's own mouse movement. Asked on the right button's press and release in the
    /// Level view (docs/guides/studio/level-view.md#fly-look).
    virtual bool setRelativeMouse(bool on) = 0;
    /// The mouse movement, in physical pixels, since the last call (or since relative mode began); zero when relative
    /// mode is off.
    virtual MouseMotion takeRelativeMotion() = 0;
};

/// Where Studio finds what it needs at run time.
struct StudioSettings {
    std::filesystem::path assets;    ///< Coney's own assets (the sandbox's layouts and textures), for the templates.
    std::filesystem::path coney;     ///< The `coney` executable Run in Coney starts.
    std::optional<std::string> disc; ///< The player's disc, passed on to Coney; none flies the level round.
    ThemeTokens theme;
    float uiScale = 1.0F; ///< The display's scale (docs/guides/debug-menu.md#ui-scale): sizes are multiplied by it.
    /// How fast the right mouse button turns the Level view's camera, as a factor of the default (1): the one setting
    /// the preferences will hold (docs/guides/studio/level-view.md#fly-look).
    float lookSensitivity = 1.0F;
    /// The preferences file (StudioPreferences), read when Studio starts and written whenever they change. Empty keeps
    /// them in memory only, as a test run does.
    std::filesystem::path preferencesFile;
};

/// One recent project as the start screen shows it.
struct RecentEntry {
    std::filesystem::path root; ///< The project's folder.
    std::string name;           ///< Its name.
    bool missing = false;       ///< The folder, or its manifest, is gone: shown as missing, and can be removed.

    friend bool operator==(const RecentEntry&, const RecentEntry&) = default;
};

/// One Console line.
struct ConsoleLine {
    enum class Level : std::uint8_t { Info, Warn, Error };
    Level level = Level::Info;
    std::string text;
};

/// Coney Studio's editor state and its user interface.
class StudioApp {
  public:
    /// The most lines the Console keeps.
    static constexpr std::size_t kConsoleLines = 2000;

    /// An editor with no project open, asking `host` for what needs the platform; `host` must outlive it.
    StudioApp(StudioHost& host, StudioSettings settings);
    /// Out of line, where platform::ImGuiModelView is complete.
    ~StudioApp();
    StudioApp(const StudioApp&) = delete;
    StudioApp& operator=(const StudioApp&) = delete;
    StudioApp(StudioApp&&) = delete;
    StudioApp& operator=(StudioApp&&) = delete;

    /// Makes a new project called `name` in `root` with the level template `levelTemplate` (levelTemplates()) as
    /// `levels/main.layout`, its textures copied into `textures/`, then opens it. Fails as Project::create() does, with
    /// ErrorCode::InvalidArgument for an unknown template and ErrorCode::NotFound or Io for a template file missing
    /// from the assets.
    std::expected<void, Error> newProject(const std::filesystem::path& root, std::string_view name,
                                          std::string_view levelTemplate);
    /// Opens the project at `root` and its `[play] level`, when it has one that loads, and puts it first in the recent
    /// projects. Fails as Project::open() does.
    std::expected<void, Error> openProject(const std::filesystem::path& root);
    /// Opens the recent project in `root` (openProject()); fails with ErrorCode::NotFound, naming the folder, when it
    /// is missing, and leaves it on the list to be removed.
    std::expected<void, Error> openRecent(const std::filesystem::path& root);
    /// Opens the *New project* dialog, with its folder set to the projects folder, which is made if it is not there
    /// yet.
    void askNewProject();
    /// Opens the folder picker for *Open project*, started in the projects folder (made if it is not there yet).
    void askOpenProject();

    /// The folder New project suggests and Open project starts in: the user's choice (setProjectsFolder()), else the
    /// default in their documents folder; nothing when the platform reports no documents or home folder.
    [[nodiscard]] std::optional<std::filesystem::path> projectsFolder() const;
    /// The default projects folder for this user: `Coney Studio/Projects` in the documents folder, else the home
    /// folder.
    [[nodiscard]] std::optional<std::filesystem::path> defaultProjectsFolder() const;
    /// Makes `folder` the projects folder and saves the preferences; nothing puts the default back. Fails as
    /// savePreferences() does (the choice is kept for the run).
    std::expected<void, Error> setProjectsFolder(std::optional<std::filesystem::path> folder);
    /// Makes the projects folder if it is not there (the first use), and returns it. Fails with ErrorCode::Io when it
    /// cannot be made and ErrorCode::NotFound when there is none.
    std::expected<std::filesystem::path, Error> ensureProjectsFolder();
    /// The recent projects, most recent first, each marked missing when its project is gone as of the last
    /// refreshRecent() (every change to the list and the start screen's drawing do it).
    [[nodiscard]] const std::vector<RecentEntry>& recentProjects() const { return m_recent; }
    /// Looks at the disk again for which recent projects are missing.
    void refreshRecent();
    /// Takes the project in `root` off the recent list and saves. False when it was not on it.
    bool removeRecent(const std::filesystem::path& root);
    /// The folder the *New project* dialog holds now.
    [[nodiscard]] const std::string& newProjectFolder() const { return m_newFolder; }
    /// What is kept per user.
    [[nodiscard]] const StudioPreferences& preferences() const { return m_preferences; }
    /// Opens the level at `projectPath` in the open project, puts the camera at its first view and clears the
    /// selection. Fails with ErrorCode::InvalidArgument without a project, as Project::resolve() does, and as
    /// LevelDocument::load() does.
    std::expected<void, Error> openLevel(std::string_view projectPath);
    /// Selects `entity` (every panel shows it), or clears the selection; an unknown id clears it.
    void select(std::optional<EntityId> entity);
    /// Glides the Level view's camera to frame the selection (*F*, and a double-click in the Hierarchy); nothing
    /// without one. The glide starts at the Level view's next draw, which knows the panel's shape, and runs over the
    /// camera's steps from there (FlyCamera::frame()).
    void frameSelection();
    /// Puts the Level view's camera at the open level's camera view `name`. False when there is no such view.
    bool goToView(std::string_view name);
    /// Play as Run in Coney: starts `coney` on the open level (runInConeyCommand()). Fails with
    /// ErrorCode::InvalidArgument when no level is open, and as StudioHost::startProcess() does.
    std::expected<void, Error> runInConey();

    /// One frame of the interface: `steps` fixed steps of `stepSeconds` for the Level view's camera, then every panel
    /// drawn with the camera `alpha` of the way between its last two steps. Needs a current Dear ImGui context with a
    /// frame begun (between ImGui::NewFrame() and ImGui::Render()), as the overlay's view does, so the tests run it
    /// over a bare context.
    void frame(std::uint32_t steps, float stepSeconds, float alpha);
    /// Builds the default docked layout at the next frame (*Window > Reset layout*).
    void resetLayout() { m_layoutPending = true; }

    /// The fonts the panels draw with (Inter, Inter Bold, JetBrains Mono), from the font atlas the host built; null
    /// for Dear ImGui's default, as in the tests.
    void setFonts(ImFont* interfaceFont, ImFont* boldFont, ImFont* codeFont);
    /// Adds a Console line (and keeps at most kConsoleLines).
    void log(ConsoleLine::Level level, std::string text);

    /// The open project, if any.
    [[nodiscard]] const std::optional<Project>& project() const { return m_project; }
    /// The open level, or null.
    [[nodiscard]] const LevelDocument* level() const { return m_level ? &*m_level : nullptr; }
    /// The selected entity, or nothing (the level itself).
    [[nodiscard]] std::optional<EntityId> selection() const { return m_selection; }
    /// The Level view's camera as of its newest step.
    [[nodiscard]] const FlyCamera& camera() const { return m_camera.current(); }
    /// The Console's lines, oldest first.
    [[nodiscard]] const std::deque<ConsoleLine>& console() const { return m_console; }
    /// What Studio was started with.
    [[nodiscard]] const StudioSettings& settings() const { return m_settings; }
    /// The Inspector's page now (the selection's, or the level's), or null with no level open.
    [[nodiscard]] const debug::MenuPage* inspector() const { return m_inspector.get(); }
    /// The window title: Studio's name, the project's and the level's.
    [[nodiscard]] std::string windowTitle() const;
    /// Whether the user asked to quit (*File > Quit*).
    [[nodiscard]] bool quitRequested() const { return m_quit; }

  private:
    // The menu bar: File (new, open, quit), Play (Run in Coney, also Ctrl+P), Window (reset the layout).
    void drawMenuBar();
    // The bar under the menu, `height` pixels: the tools (disabled until the gizmos) and Play in the middle.
    void drawToolbar(float height);
    // The window below `top` pixels that holds the dock space, building the default layout when asked.
    void drawDockSpace(float top);
    // Docks the six panels into `dockspace` in Godot's arrangement.
    void buildDefaultLayout(unsigned int dockspace);
    // The Hierarchy: the level's row, then one row per entity; a click selects.
    void drawHierarchy();
    // The Level view: the camera's steps, the level drawn by the host, the markers, and a click's selection.
    void drawLevelView(std::uint32_t steps, float stepSeconds, float alpha);
    // The Game view's placeholder until Play runs in Studio.
    void drawGameView();
    // The Inspector: the selection's groups, each a header over the debug menu renderer's items.
    void drawInspector();
    // The File system: the project's tree from `project:/`, or New and Open buttons without one.
    void drawFileSystem();
    // One folder of the File system's tree, read once and kept until Refresh; a double-click opens a level.
    void drawFolder(const std::string& projectPath);
    // The point entities as named dots and the selection's box, over the Level view at (`left`, `top`).
    void drawLevelMarkers(const LevelView& view, float left, float top);
    // The Console's lines, in the code font, following the newest.
    void drawConsole();
    // The New project dialog: a name, the folder it goes in and the first level's template.
    void drawNewProjectDialog();
    // The Level view's input for this frame from Dear ImGui's mouse and keys, while the panel is hovered or flying.
    FlyInput readFlyInput(bool hovered);
    // Starts or ends flying, asking the host to take the cursor into relative mode and give it back.
    void setFlying(bool flying);
    // The camera blended `alpha` of the way between its last two steps.
    [[nodiscard]] ViewPose blendedPose(float alpha) const;
    // Rebuilds the Inspector's page for the selection, and its groups' pages.
    void refreshInspector();
    // Starts the camera's glide to frame the selection in the Level view's current shape.
    void startFraming();
    // Saves the preferences to the preferences file, if there is one; a failure goes to the Console.
    void savePreferencesQuietly();
    // The start screen: the Level view's place until a project is open. New and Open buttons and the recent projects.
    void drawStartScreen();
    // The Preferences window (Edit > Preferences): the projects folder.
    void drawPreferencesDialog();
    // Logs a failed action's error.
    void report(std::string_view what, const Error& error);

    StudioHost& m_host;
    StudioSettings m_settings;
    std::optional<Project> m_project;
    std::optional<LevelDocument> m_level;
    std::optional<EntityId> m_selection;
    Interpolated<FlyCamera> m_camera;
    std::deque<ConsoleLine> m_console;
    debug::MenuModel m_model; // the Inspector's renderer needs one for pins
    std::unique_ptr<platform::ImGuiModelView> m_view;
    std::shared_ptr<debug::MenuPage> m_inspector;
    std::vector<std::pair<std::string, std::shared_ptr<debug::MenuPage>>> m_groups; // the Inspector's open groups
    std::map<std::string, std::vector<ProjectEntry>> m_folders; // the File system's listings, by project path
    bool m_layoutPending = true;
    bool m_flying = false;
    bool m_relativeMouse = false;              // the host took the cursor and reports the movement (setFlying)
    bool m_framePending = false;               // frameSelection() was asked; the Level view starts the glide
    float m_levelHalfWidth = kLevelHalfHeight; // the Level view's last view window width, for framing
    bool m_scrollToSelection = false;          // the Hierarchy brings the selection's row into view at its next draw
    float m_pendingLookX = 0.0F;               // mouse movement not yet given to a camera step
    float m_pendingLookY = 0.0F;
    float m_pendingWheel = 0.0F;
    ImFont* m_boldFont = nullptr;
    ImFont* m_codeFont = nullptr;
    bool m_quit = false;
    bool m_newProjectOpen = false;
    bool m_preferencesOpen = false;
    StudioPreferences m_preferences;
    std::vector<RecentEntry> m_recent;
    double m_recentCheckedAt = 0.0;   // Dear ImGui's time of the last refreshRecent() from the start screen
    std::string m_projectsFolderText; // the Preferences window's text for the projects folder
    std::string m_newName = "my-project";
    std::string m_newFolder;
    int m_newTemplate = 0;
};

} // namespace coney::studio
