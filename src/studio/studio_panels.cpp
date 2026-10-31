// SPDX-License-Identifier: GPL-3.0-or-later
// Coney Studio's interface: the menu bar, the toolbar, the dock space with the default layout, and the panels of the
// prototype other than the Level view (docs/guides/studio/prototype.md; the Level view is studio_level_view.cpp). The
// state they act on is studio_app.cpp's. @coney-only the editor;
// the original has none.
#include <algorithm>
#include <array>
#include <format>
#include <string>

#include <imgui.h>
#include <imgui_internal.h> // the DockBuilder API
#include <imgui_stdlib.h>   // InputText on std::string

#include "platform/imgui_model_view.h"
#include "studio/inspector.h"
#include "studio/level_templates.h"
#include "studio/panel_colours.h"
#include "studio/studio_app.h"

namespace coney::studio {

namespace {

// The dock space's id and the window that holds it.
constexpr const char* kDockHost = "##StudioDockHost";
constexpr const char* kDockSpace = "StudioDockSpace";
// The toolbar's height at UI scale 1, pixels.
constexpr float kToolbarHeight = 34.0F;

} // namespace

void StudioApp::frame(std::uint32_t steps, float stepSeconds, float alpha) {
    applyTheme(m_settings.theme, m_settings.uiScale, ImGui::GetStyle());
    drawMenuBar();
    const float toolbar = kToolbarHeight * m_settings.uiScale;
    drawToolbar(toolbar);
    drawDockSpace(ImGui::GetFrameHeight() + toolbar);
    drawHierarchy();
    drawLevelView(steps, stepSeconds, alpha);
    drawGameView();
    drawInspector();
    drawFileSystem();
    drawConsole();
    drawNewProjectDialog();
    drawPreferencesDialog();
}

void StudioApp::drawMenuBar() {
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New project...")) {
            askNewProject();
        }
        if (ImGui::MenuItem("Open project...")) {
            askOpenProject();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Quit")) {
            m_quit = true;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Preferences...")) {
            m_preferencesOpen = true;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Play")) {
        if (ImGui::MenuItem("Run in Coney", "Ctrl+P", false, m_level.has_value())) {
            if (auto run = runInConey(); !run) {
                report("run in Coney", run.error());
            }
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Window")) {
        if (ImGui::MenuItem("Reset layout")) {
            resetLayout();
        }
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
    // F frames the selection, unless a text box has the keys (docs/guides/studio/level-view.md#fly-camera).
    if (!ImGui::GetIO().WantTextInput && !m_flying && ImGui::IsKeyPressed(ImGuiKey_F, false) &&
        !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyAlt) {
        frameSelection();
    }
    // Ctrl+P plays, as the keymap gives it (docs/guides/studio/level-view.md#keymap).
    if (m_level && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_P, ImGuiInputFlags_RouteGlobal)) {
        if (auto run = runInConey(); !run) {
            report("run in Coney", run.error());
        }
    }
}

void StudioApp::drawToolbar(float height) {
    // A bar under the menu: the tools Q to Y (waiting on the gizmos' build issue), then Play in the middle, as Unity's.
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, viewport->WorkPos.y));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, height));
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings |
                                        ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0F * m_settings.uiScale, 4.0F * m_settings.uiScale));
    // The toolbar sits on the dark background with the menu bar, as Godot's top bar does.
    ImGui::PushStyleColor(ImGuiCol_WindowBg, imguiColour(derivePalette(m_settings.theme).dark2));
    ImGui::Begin("##StudioToolbar", nullptr, kFlags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    ImGui::BeginDisabled();
    for (const char* tool : {"Q", "W", "E", "R", "Y"}) {
        ImGui::Button(tool);
        ImGui::SameLine();
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("The tools and gizmos come with their own build issue");
    const float play = ImGui::CalcTextSize("Run in Coney").x + (ImGui::GetStyle().FramePadding.x * 2.0F);
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - play) * 0.5F);
    ImGui::BeginDisabled(!m_level.has_value());
    // Play is the one filled accent button, as Godot's run buttons stand out from the bar.
    ImGui::PushStyleColor(ImGuiCol_Button, accentColour(m_settings.theme, 0.85F));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, accentColour(m_settings.theme));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, accentColour(m_settings.theme, 0.7F));
    if (ImGui::Button("Run in Coney")) {
        if (auto run = runInConey(); !run) {
            report("run in Coney", run.error());
        }
    }
    ImGui::PopStyleColor(3);
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Play: start Coney on the open level in its own window (Ctrl+P)");
    ImGui::End();
}

void StudioApp::drawDockSpace(float top) {
    // A window filling the rest of the screen holds the dock space every panel docks into.
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + top));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, viewport->Size.y - top));
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings |
                                        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
    // The dock host shows through the gaps between the panels: the dark background, as between Godot's docks.
    ImGui::PushStyleColor(ImGuiCol_WindowBg, imguiColour(derivePalette(m_settings.theme).dark2));
    ImGui::Begin(kDockHost, nullptr, kFlags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    const ImGuiID dockspace = ImGui::GetID(kDockSpace);
    if (m_layoutPending || ImGui::DockBuilderGetNode(dockspace) == nullptr) {
        buildDefaultLayout(dockspace);
        m_layoutPending = false;
    }
    // Dear ImGui draws the splitters between docked panels in the border colour: the dark background here, so the gaps
    // read as space between the panels, as Godot's do; a hovered splitter still lights up in the accent.
    ImGui::PushStyleColor(ImGuiCol_Border, imguiColour(derivePalette(m_settings.theme).dark2));
    ImGui::DockSpace(dockspace, ImVec2(0.0F, 0.0F));
    ImGui::PopStyleColor();
    ImGui::End();
}

void StudioApp::buildDefaultLayout(unsigned int dockspace) {
    // Godot's arrangement (docs/guides/studio/editor-experience.md#layout-default): the Hierarchy over the File system
    // on the left, the Inspector on the right, the Level and Game views side by side in the middle, the Console under
    // them.
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(dockspace);
    ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace, viewport->Size);
    ImGuiID middle = dockspace;
    const ImGuiID left = ImGui::DockBuilderSplitNode(middle, ImGuiDir_Left, 0.18F, nullptr, &middle);
    const ImGuiID right = ImGui::DockBuilderSplitNode(middle, ImGuiDir_Right, 0.27F, nullptr, &middle);
    const ImGuiID bottom = ImGui::DockBuilderSplitNode(middle, ImGuiDir_Down, 0.26F, nullptr, &middle);
    ImGuiID levelNode = middle;
    const ImGuiID gameNode = ImGui::DockBuilderSplitNode(levelNode, ImGuiDir_Right, 0.4F, nullptr, &levelNode);
    ImGuiID hierarchyNode = left;
    const ImGuiID filesNode = ImGui::DockBuilderSplitNode(hierarchyNode, ImGuiDir_Down, 0.45F, nullptr, &hierarchyNode);
    ImGui::DockBuilderDockWindow(panels::kHierarchy, hierarchyNode);
    ImGui::DockBuilderDockWindow(panels::kFileSystem, filesNode);
    ImGui::DockBuilderDockWindow(panels::kLevel, levelNode);
    ImGui::DockBuilderDockWindow(panels::kGame, gameNode);
    ImGui::DockBuilderDockWindow(panels::kConsole, bottom);
    ImGui::DockBuilderDockWindow(panels::kInspector, right);
    ImGui::DockBuilderFinish(dockspace);
}

void StudioApp::drawHierarchy() {
    if (!ImGui::Begin(panels::kHierarchy)) {
        ImGui::End();
        return;
    }
    if (!m_level) {
        ImGui::TextDisabled("No level open");
        ImGui::End();
        return;
    }
    // The level's row: chosen, the Inspector shows its settings; under it one row per entity, flat as a layout is.
    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (!m_selection) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    const bool open = ImGui::TreeNodeEx("##level", flags, "%s", m_level->title().c_str());
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        select(std::nullopt);
    }
    if (open) {
        for (const Entity& entity : m_level->entities()) {
            const bool chosen = m_selection == entity.id;
            const std::string label = std::format("{}##{}", entity.name, entity.id);
            if (entity.kind != EntityKind::Primitive) {
                ImGui::PushStyleColor(ImGuiCol_Text, markerColour(entity.kind));
            }
            if (ImGui::Selectable(label.c_str(), chosen, ImGuiSelectableFlags_AllowDoubleClick)) {
                select(entity.id);
                m_scrollToSelection = false; // already in view
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    frameSelection(); // Unity's double-click in the Hierarchy frames the object
                }
            }
            if (entity.kind != EntityKind::Primitive) {
                ImGui::PopStyleColor();
            }
            if (chosen && m_scrollToSelection) {
                ImGui::SetScrollHereY(); // chosen in the Level view or from the command line: bring its row into view
                m_scrollToSelection = false;
            }
        }
        ImGui::TreePop();
    }
    ImGui::End();
}

void StudioApp::drawGameView() {
    if (ImGui::Begin(panels::kGame)) {
        ImGui::TextWrapped("The game runs here once Play runs inside Studio. Until then, Run in Coney (Ctrl+P) "
                           "starts Coney on the open level in its own window.");
        ImGui::Spacing();
        ImGui::BeginDisabled(!m_level.has_value());
        if (ImGui::Button("Run in Coney")) {
            if (auto run = runInConey(); !run) {
                report("run in Coney", run.error());
            }
        }
        ImGui::EndDisabled();
        ImGui::TextDisabled(m_settings.disc ? "Disc: %s (plays as Rembrandt)" : "Disc: none (the level is flown round)",
                            m_settings.disc ? m_settings.disc->c_str() : "");
    }
    ImGui::End();
}

void StudioApp::drawInspector() {
    if (!ImGui::Begin(panels::kInspector)) {
        ImGui::End();
        return;
    }
    if (!m_inspector) {
        ImGui::TextDisabled("Nothing to inspect");
        ImGui::End();
        return;
    }
    // Each group as a header with its lines, drawn by the debug menu's own renderer (docs/guides/debug-menu.md). The
    // headers are bold on a faint accent band, as Godot's Inspector tints its sections, fainter than a selection.
    const ThemePalette palette = derivePalette(m_settings.theme);
    for (const auto& [title, group] : m_groups) {
        ImGui::PushStyleColor(ImGuiCol_Header, imguiColour(palette.section));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, imguiColour(palette.selection));
        if (m_boldFont != nullptr) {
            ImGui::PushFont(m_boldFont, 0.0F);
        }
        const bool open = ImGui::CollapsingHeader(title.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
        if (m_boldFont != nullptr) {
            ImGui::PopFont();
        }
        ImGui::PopStyleColor(2);
        if (open) {
            for (const auto& item : group->items()) {
                m_view->drawItem(item, std::format("{}/{}/{}", kInspectorTitle, title, item.label));
            }
        }
    }
    ImGui::End();
}

void StudioApp::drawFileSystem() {
    if (!ImGui::Begin(panels::kFileSystem)) {
        ImGui::End();
        return;
    }
    if (!m_project) {
        ImGui::TextDisabled("No project open");
        if (ImGui::Button("New project...")) {
            askNewProject();
        }
        ImGui::SameLine();
        if (ImGui::Button("Open project...")) {
            askOpenProject();
        }
        ImGui::End();
        return;
    }
    if (ImGui::SmallButton("Refresh")) {
        m_folders.clear();
    }
    if (ImGui::TreeNodeEx("project:/", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) {
        drawFolder(std::string(kProjectScheme));
        ImGui::TreePop();
    }
    ImGui::End();
}

void StudioApp::drawFolder(const std::string& projectPath) {
    const Project* owner = m_project ? &*m_project : nullptr;
    if (owner == nullptr) {
        return;
    }
    // A folder's listing is read once and kept until Refresh or a project opened.
    auto found = m_folders.find(projectPath);
    if (found == m_folders.end()) {
        auto listed = owner->list(projectPath);
        found = m_folders.emplace(projectPath, listed ? std::move(*listed) : std::vector<ProjectEntry>{}).first;
    }
    const std::vector<ProjectEntry> entries = found->second; // a copy: a click on a row opens a level mid-loop
    for (const ProjectEntry& entry : entries) {
        if (entry.folder) {
            if (ImGui::TreeNodeEx(entry.projectPath.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth, "%s/",
                                  entry.name.c_str())) {
                drawFolder(entry.projectPath);
                ImGui::TreePop();
            }
            continue;
        }
        const bool isLevel = entry.name.ends_with(sandbox::kLayoutExtension);
        const bool open = m_level && owner->projectPathOf(m_level->path()).value_or("") == entry.projectPath;
        ImGui::TreeNodeEx(entry.projectPath.c_str(),
                          ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                              ImGuiTreeNodeFlags_SpanAvailWidth | (open ? ImGuiTreeNodeFlags_Selected : 0),
                          "%s", entry.name.c_str());
        if (isLevel && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            if (auto opened = openLevel(entry.projectPath); !opened) {
                report("open the level", opened.error());
            }
        }
        if (isLevel) {
            ImGui::SetItemTooltip("Double-click to open in the Level view");
        }
    }
}

void StudioApp::drawConsole() {
    if (!ImGui::Begin(panels::kConsole)) {
        ImGui::End();
        return;
    }
    if (m_codeFont != nullptr) {
        ImGui::PushFont(m_codeFont, 0.0F);
    }
    for (const ConsoleLine& line : m_console) {
        const ImVec4 colour = line.level == ConsoleLine::Level::Error  ? ImVec4(1.0F, 0.45F, 0.42F, 1.0F)
                              : line.level == ConsoleLine::Level::Warn ? ImVec4(1.0F, 0.8F, 0.4F, 1.0F)
                                                                       : ImGui::GetStyle().Colors[ImGuiCol_Text];
        ImGui::TextColored(colour, "%s", line.text.c_str());
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0F); // follow the newest line unless the user scrolled up
    }
    if (m_codeFont != nullptr) {
        ImGui::PopFont();
    }
    ImGui::End();
}

void StudioApp::drawNewProjectDialog() {
    if (m_newProjectOpen) {
        ImGui::OpenPopup("New project");
        m_newProjectOpen = false;
    }
    ImGui::SetNextWindowSize(ImVec2(520.0F * m_settings.uiScale, 0.0F));
    if (!ImGui::BeginPopupModal("New project", nullptr, ImGuiWindowFlags_NoResize)) {
        return;
    }
    // The name, the folder it goes in (the project is a new folder of that name inside it) and the first level.
    ImGui::InputText("Name", &m_newName);
    ImGui::InputText("In folder", &m_newFolder);
    ImGui::SameLine();
    if (ImGui::Button("Browse...")) {
        const std::filesystem::path typed = pathFromUtf8(m_newFolder);
        m_host.pickFolder("Where to make the project", typed.empty() ? projectsFolder() : std::optional(typed),
                          [this](std::optional<std::filesystem::path> folder) {
                              if (folder) {
                                  m_newFolder = pathToUtf8(*folder);
                              }
                          });
    }
    const auto templates = levelTemplates();
    const std::string chosen(templates[static_cast<std::size_t>(m_newTemplate)]);
    if (ImGui::BeginCombo("First level", chosen.c_str())) {
        for (std::size_t i = 0; i < templates.size(); ++i) {
            if (ImGui::Selectable(std::string(templates[i]).c_str(), static_cast<int>(i) == m_newTemplate)) {
                m_newTemplate = static_cast<int>(i);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::TextDisabled("basic: a ground, a spawn point and a view; the others copy Coney's sandbox courses");
    const bool ready = validProjectName(m_newName) && !m_newFolder.empty();
    ImGui::BeginDisabled(!ready);
    if (ImGui::Button("Create")) {
        if (auto made = newProject(pathFromUtf8(m_newFolder) / m_newName, m_newName, chosen); !made) {
            report("make the project", made.error());
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void StudioApp::drawStartScreen() {
    const float scale = m_settings.uiScale;
    // Which recent projects are still there is looked at again every couple of seconds, so a project moved while the
    // screen is up is marked without a restart.
    if (const double now = ImGui::GetTime(); m_recentCheckedAt == 0.0 || now - m_recentCheckedAt >= 2.0) {
        refreshRecent();
        m_recentCheckedAt = now > 0.0 ? now : 1.0;
    }
    ImGui::Dummy(ImVec2(0.0F, 28.0F * scale));
    ImGui::Indent(36.0F * scale);
    const float rowWidth = std::max(ImGui::GetContentRegionAvail().x - (36.0F * scale), 100.0F * scale);
    if (m_boldFont != nullptr) {
        ImGui::PushFont(m_boldFont, 0.0F);
    }
    ImGui::TextUnformatted("Coney Studio");
    if (m_boldFont != nullptr) {
        ImGui::PopFont();
    }
    ImGui::TextDisabled("Make a project, or open one.");
    ImGui::Spacing();
    if (ImGui::Button("New project...")) {
        askNewProject();
    }
    ImGui::SameLine();
    if (ImGui::Button("Open project...")) {
        askOpenProject();
    }
    ImGui::Dummy(ImVec2(0.0F, 12.0F * scale));
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextUnformatted("Recent projects");
    ImGui::Spacing();
    if (m_recent.empty()) {
        ImGui::TextDisabled("The projects you make or open are listed here, the newest first.");
    }
    // A copy: a click that opens a project, or removes one, changes the list mid-loop.
    const std::vector<RecentEntry> entries = m_recent;
    const float removeWidth = ImGui::CalcTextSize("Remove").x + (2.0F * ImGui::GetStyle().FramePadding.x);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const RecentEntry& entry = entries[i];
        const std::string path = pathToUtf8(entry.root);
        ImGui::PushID(static_cast<int>(i));
        const float startX = ImGui::GetCursorPosX();
        const float nameWidth = rowWidth - removeWidth - ImGui::GetStyle().ItemSpacing.x;
        // A missing project is named in the warning colour and cannot be opened, only removed.
        ImGui::BeginDisabled(entry.missing);
        if (ImGui::Selectable(entry.name.c_str(), false, ImGuiSelectableFlags_None, ImVec2(nameWidth, 0.0F))) {
            if (auto opened = openRecent(entry.root); !opened) {
                report("open the project", opened.error());
            }
        }
        ImGui::EndDisabled();
        if (!entry.missing && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Open %s", path.c_str());
        }
        ImGui::SameLine(startX + rowWidth - removeWidth);
        if (ImGui::SmallButton("Remove")) {
            removeRecent(entry.root);
        }
        if (entry.missing) {
            ImGui::TextColored(ImVec4(1.0F, 0.8F, 0.4F, 1.0F), "Missing: %s", path.c_str());
        } else {
            ImGui::TextDisabled("%s", path.c_str());
        }
        ImGui::Spacing();
        ImGui::PopID();
    }
    ImGui::Dummy(ImVec2(0.0F, 8.0F * scale));
    ImGui::Separator();
    ImGui::Spacing();
    const auto folder = projectsFolder();
    ImGui::TextDisabled("Projects folder: %s",
                        folder ? pathToUtf8(*folder).c_str() : "none (no documents or home folder)");
    ImGui::SameLine();
    if (ImGui::SmallButton("Change...")) {
        m_preferencesOpen = true;
    }
    ImGui::Unindent(36.0F * scale);
}

void StudioApp::drawPreferencesDialog() {
    if (m_preferencesOpen) {
        const auto folder = projectsFolder();
        m_projectsFolderText = folder ? pathToUtf8(*folder) : std::string();
        ImGui::OpenPopup("Preferences");
        m_preferencesOpen = false;
    }
    ImGui::SetNextWindowSize(ImVec2(520.0F * m_settings.uiScale, 0.0F));
    if (!ImGui::BeginPopupModal("Preferences", nullptr, ImGuiWindowFlags_NoResize)) {
        return;
    }
    // The projects folder: where New project suggests making a project and Open project starts, made when first used.
    ImGui::InputText("Projects folder", &m_projectsFolderText);
    ImGui::SameLine();
    if (ImGui::Button("Browse...")) {
        const std::filesystem::path typed = pathFromUtf8(m_projectsFolderText);
        m_host.pickFolder("Choose the projects folder", typed.empty() ? projectsFolder() : std::optional(typed),
                          [this](std::optional<std::filesystem::path> chosen) {
                              if (chosen) {
                                  m_projectsFolderText = pathToUtf8(*chosen);
                              }
                          });
    }
    if (ImGui::Button("Use the default")) {
        const auto standard = defaultProjectsFolder();
        m_projectsFolderText = standard ? pathToUtf8(*standard) : std::string();
    }
    ImGui::TextDisabled("New project suggests this folder and Open project starts in it.");
    if (ImGui::Button("Apply")) {
        // The default is kept as "no choice", so it follows the user's documents folder if that moves.
        std::optional<std::filesystem::path> chosen = pathFromUtf8(m_projectsFolderText);
        if (chosen->empty() || chosen == defaultProjectsFolder()) {
            chosen.reset();
        }
        if (auto saved = setProjectsFolder(std::move(chosen)); !saved) {
            report("save Studio's preferences", saved.error());
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

} // namespace coney::studio
