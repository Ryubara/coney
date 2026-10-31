// SPDX-License-Identifier: GPL-3.0-or-later
// Coney Studio's state and actions: the project, the level, the selection, Play. The panels that draw them are
// studio_panels.cpp. @coney-only the editor; the original has none.
#include "studio/studio_app.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

#include "platform/imgui_model_view.h"
#include "studio/inspector.h"
#include "studio/level_templates.h"
#include "studio/run_in_coney.h"

namespace coney::studio {

namespace {

// The level file a new project starts with.
constexpr std::string_view kFirstLevel = "main.layout";

// The camera of a level: at its first view (a layout always has one).
FlyCamera cameraOf(const LevelDocument& level) { return FlyCamera::at(level.layout().views.front()); }

// Reads the whole file at `path` as text.
std::expected<std::string, Error> readText(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return fail(ErrorCode::NotFound, std::format("could not read {}", path.string()));
    }
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

} // namespace

StudioApp::StudioApp(StudioHost& host, StudioSettings settings)
    : m_host(host), m_settings(std::move(settings)), m_camera(FlyCamera({0.0F, -8.0F, 3.0F}, 0.0F, -12.0F)),
      m_view(std::make_unique<platform::ImGuiModelView>(m_model)) {
    m_view->setUiScale(m_settings.uiScale);
    log(ConsoleLine::Level::Info, "Coney Studio: File > New project or Open project to start");
    // Studio's preferences: the projects folder and the recent projects. A file that cannot be read is reported, and
    // the defaults used; the next change then replaces it.
    if (!m_settings.preferencesFile.empty()) {
        if (auto loaded = loadPreferences(m_settings.preferencesFile)) {
            m_preferences = std::move(*loaded);
        } else {
            log(ConsoleLine::Level::Warn,
                "could not read Studio's preferences, using the defaults: " + loaded.error().message);
        }
    }
    refreshRecent();
}

StudioApp::~StudioApp() = default;

void StudioApp::setFonts(ImFont* interfaceFont, ImFont* boldFont, ImFont* codeFont) {
    static_cast<void>(interfaceFont); // the atlas's first font is Dear ImGui's default already
    m_boldFont = boldFont;
    m_codeFont = codeFont;
}

std::expected<void, Error> StudioApp::newProject(const std::filesystem::path& root, std::string_view name,
                                                 std::string_view levelTemplate) {
    // 1. The first level's text, from the template.
    const auto templates = levelTemplates();
    if (std::ranges::find(templates, levelTemplate) == templates.end()) {
        return fail(ErrorCode::InvalidArgument, std::format("no level template called {}", levelTemplate));
    }
    const std::filesystem::path sandbox = m_settings.assets / "sandbox";
    std::string text;
    if (levelTemplate == kBasicTemplate) {
        text = basicLevelText();
    } else {
        auto shipped = readText(sandbox / (std::string(levelTemplate) + std::string(sandbox::kLayoutExtension)));
        if (!shipped) {
            return std::unexpected(std::move(shipped.error()));
        }
        text = rehomeTextures(*shipped, "../textures/");
    }
    // 2. The sandbox's textures and their licence (Coney's own CC0 assets), listed first so a missing assets folder
    // refuses before any project is made.
    std::vector<std::filesystem::path> shippedFiles;
    std::error_code error;
    std::filesystem::directory_iterator listing(sandbox, error);
    if (error) {
        return fail(ErrorCode::NotFound,
                    std::format("could not read the sandbox assets {}: {}", sandbox.string(), error.message()));
    }
    for (const std::filesystem::directory_iterator end; listing != end; listing.increment(error)) {
        if (error) {
            return fail(ErrorCode::Io, std::format("could not list {}: {}", sandbox.string(), error.message()));
        }
        if (listing->path().extension() == ".png" || listing->path().filename() == "License.txt") {
            shippedFiles.push_back(listing->path());
        }
    }
    // 3. The project, then those files beside the level's textures folder.
    auto project = Project::create(root, name, kFirstLevel, text);
    if (!project) {
        return std::unexpected(std::move(project.error()));
    }
    for (const std::filesystem::path& file : shippedFiles) {
        std::filesystem::copy_file(file, project->root() / "textures" / file.filename(),
                                   std::filesystem::copy_options::overwrite_existing, error);
        if (error) {
            return fail(ErrorCode::Io, std::format("could not copy {} (the project in {} is incomplete): {}",
                                                   file.string(), project->root().string(), error.message()));
        }
    }
    log(ConsoleLine::Level::Info, std::format("made project {} in {}", name, project->root().string()));
    return openProject(project->root());
}

std::expected<void, Error> StudioApp::openProject(const std::filesystem::path& root) {
    auto project = Project::open(root);
    if (!project) {
        return std::unexpected(std::move(project.error()));
    }
    m_project = std::move(*project);
    addRecentProject(m_preferences, RecentProject{.root = m_project->root(), .name = m_project->manifest().name});
    savePreferencesQuietly();
    refreshRecent();
    m_level.reset();
    m_folders.clear();
    select(std::nullopt);
    log(ConsoleLine::Level::Info, std::format("opened project {}", m_project->manifest().name));
    if (const std::string& level = m_project->manifest().playLevel; !level.empty()) {
        if (auto opened = openLevel(level); !opened) {
            report("open the project's level", opened.error());
        }
    }
    return {};
}

std::expected<void, Error> StudioApp::openLevel(std::string_view projectPath) {
    if (!m_project) {
        return fail(ErrorCode::InvalidArgument, "open a project first: a level is a file of one");
    }
    const auto path = m_project->resolve(projectPath);
    if (!path) {
        return std::unexpected(path.error());
    }
    auto level = LevelDocument::load(*path);
    if (!level) {
        return std::unexpected(std::move(level.error()));
    }
    m_level = std::move(*level);
    m_camera.reset(cameraOf(*m_level));
    m_selection.reset();
    refreshInspector();
    log(ConsoleLine::Level::Info, std::format("opened level {}: {} entities", projectPath, m_level->entities().size()));
    return {};
}

void StudioApp::select(std::optional<EntityId> entity) {
    m_selection = (entity && m_level && m_level->entity(*entity) != nullptr) ? entity : std::nullopt;
    m_scrollToSelection = m_selection.has_value();
    refreshInspector();
}

void StudioApp::frameSelection() {
    if (m_level && m_selection) {
        m_framePending = true;
    }
}

void StudioApp::startFraming() {
    if (!m_level || !m_selection) {
        return;
    }
    // The narrower of the view window's two halves, so the selection fits a tall panel as well as a wide one.
    m_camera.current().frame(m_level->bounds(*m_level->entity(*m_selection)),
                             std::min(kLevelHalfHeight, m_levelHalfWidth));
}

bool StudioApp::goToView(std::string_view name) {
    if (!m_level) {
        return false;
    }
    for (const auto& view : m_level->layout().views) {
        if (view.name == name) {
            m_camera.reset(FlyCamera::at(view)); // a jump: nothing to blend from
            return true;
        }
    }
    return false;
}

std::expected<void, Error> StudioApp::runInConey() {
    if (!m_level || m_level->path().empty()) {
        return fail(ErrorCode::InvalidArgument, "open a level first: Run in Coney plays the open level");
    }
    RunRequest request{.coney = m_settings.coney, .level = m_level->path(), .disc = m_settings.disc};
    if (m_project && !m_project->manifest().spawn.empty()) {
        request.spawn = m_project->manifest().spawn;
    }
    const std::vector<std::string> command = runInConeyCommand(request);
    if (auto started = m_host.startProcess(command); !started) {
        return std::unexpected(std::move(started.error()));
    }
    log(ConsoleLine::Level::Info, m_settings.disc
                                      ? std::string("Run in Coney: playing the level as Rembrandt")
                                      : std::string("Run in Coney: no disc set, so the level is flown round "
                                                    "(start Studio with --disc to play it)"));
    return {};
}

void StudioApp::log(ConsoleLine::Level level, std::string text) {
    m_console.push_back(ConsoleLine{.level = level, .text = std::move(text)});
    while (m_console.size() > kConsoleLines) {
        m_console.pop_front();
    }
}

std::expected<void, Error> StudioApp::openRecent(const std::filesystem::path& root) {
    if (!projectExists(root)) {
        refreshRecent();
        return fail(ErrorCode::NotFound,
                    std::format("{} is missing (moved or deleted): remove it from the list", root.string()));
    }
    return openProject(root);
}

void StudioApp::askNewProject() {
    // The dialog suggests the projects folder, made now if this is its first use; one that cannot be made is still
    // suggested, and Create says why it fails.
    m_newFolder.clear();
    if (const auto folder = projectsFolder()) {
        if (auto made = ensureFolderExists(*folder); !made) {
            report("make the projects folder", made.error());
        }
        m_newFolder = pathToUtf8(*folder);
    }
    m_newProjectOpen = true;
}

void StudioApp::askOpenProject() {
    // The picker starts in the projects folder, made now if this is its first use.
    std::optional<std::filesystem::path> start = projectsFolder();
    if (start) {
        if (auto made = ensureFolderExists(*start); !made) {
            report("make the projects folder", made.error());
            start.reset();
        }
    }
    m_host.pickFolder("Open a Coney Studio project", start, [this](std::optional<std::filesystem::path> folder) {
        if (folder) {
            if (auto opened = openProject(*folder); !opened) {
                report("open the project", opened.error());
            }
        }
    });
}

std::optional<std::filesystem::path> StudioApp::defaultProjectsFolder() const {
    return studio::defaultProjectsFolder(m_host.userFolders());
}

std::optional<std::filesystem::path> StudioApp::projectsFolder() const {
    return m_preferences.projectsFolder ? m_preferences.projectsFolder : defaultProjectsFolder();
}

std::expected<void, Error> StudioApp::setProjectsFolder(std::optional<std::filesystem::path> folder) {
    if (folder && folder->empty()) {
        folder.reset();
    }
    m_preferences.projectsFolder = std::move(folder);
    if (m_settings.preferencesFile.empty()) {
        return {};
    }
    return savePreferences(m_settings.preferencesFile, m_preferences);
}

std::expected<std::filesystem::path, Error> StudioApp::ensureProjectsFolder() {
    const auto folder = projectsFolder();
    if (!folder) {
        return fail(ErrorCode::NotFound, "the system reports no documents or home folder to keep projects in");
    }
    if (auto made = ensureFolderExists(*folder); !made) {
        return std::unexpected(std::move(made.error()));
    }
    return *folder;
}

void StudioApp::refreshRecent() {
    m_recent.clear();
    for (const RecentProject& project : m_preferences.recent) {
        m_recent.push_back(
            RecentEntry{.root = project.root, .name = project.name, .missing = !projectExists(project.root)});
    }
}

bool StudioApp::removeRecent(const std::filesystem::path& root) {
    if (!removeRecentProject(m_preferences, root)) {
        return false;
    }
    savePreferencesQuietly();
    refreshRecent();
    return true;
}

void StudioApp::savePreferencesQuietly() {
    if (m_settings.preferencesFile.empty()) {
        return;
    }
    if (auto saved = savePreferences(m_settings.preferencesFile, m_preferences); !saved) {
        report("save Studio's preferences", saved.error());
    }
}

std::string StudioApp::windowTitle() const {
    std::string title = "Coney Studio";
    if (m_project) {
        title += " - " + m_project->manifest().name;
    }
    if (m_level && !m_level->path().empty()) {
        title += " - " + m_level->path().filename().string();
    }
    return title;
}

void StudioApp::refreshInspector() {
    m_groups.clear();
    if (!m_level) {
        m_inspector.reset();
        return;
    }
    m_inspector = inspectorPage(*m_level, m_selection);
    for (const auto& item : m_inspector->items()) {
        if (item.kind == debug::ItemKind::Submenu && item.open) {
            m_groups.emplace_back(item.label, item.open());
        }
    }
}

void StudioApp::report(std::string_view what, const Error& error) {
    log(ConsoleLine::Level::Error, std::format("could not {}: {}", what, error.message));
}

ViewPose StudioApp::blendedPose(float alpha) const {
    // The camera's position and turn `alpha` of the way between its last two steps, the turn the short way round.
    const FlyCamera& from = m_camera.previous();
    const FlyCamera& to = m_camera.current();
    const anim::Vec3 position = anim::lerp(from.position(), to.position(), alpha);
    constexpr float kRadians = 3.14159265358979F / 180.0F;
    const float yaw = lerpAngle(from.yawDegrees() * kRadians, to.yawDegrees() * kRadians, alpha) / kRadians;
    const float pitch = lerp(from.pitchDegrees(), to.pitchDegrees(), alpha);
    return FlyCamera(position, yaw, pitch).pose();
}

} // namespace coney::studio
