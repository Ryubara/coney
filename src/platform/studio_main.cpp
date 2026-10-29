// SPDX-License-Identifier: GPL-3.0-or-later
// Coney Studio's entry point (docs/guides/studio/prototype.md): the window and its OpenGL context (librw's, as the
// game's), Dear ImGui with docking over SDL3 and OpenGL 3, the fonts, the main loop, and the host that gives Studio's
// panels the folder picker, child processes and the Level view's drawing. The editor itself is src/studio/. Coney's
// own; the original has no editor. SDL_main.h lets SDL provide the right entry on each platform.
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include "core/frame_clock.h"
#include "core/log.h"
#include "platform/error_dialogs.h"
#include "platform/frame_pacer.h"
#include "platform/render_engine.h"
#include "platform/studio_host.h"
#include "studio/run_in_coney.h"
#include "studio/studio_app.h"

namespace {

using coney::studio::ConsoleLine;
using coney::studio::EntityId;
using coney::studio::LevelDocument;

// The usage text of `coney-studio --help`.
constexpr std::string_view kUsage =
    "Usage: coney-studio [options]\n"
    "\n"
    "Coney Studio, the editor for Coney (docs/guides/studio/prototype.md).\n"
    "\n"
    "  --project DIR          open the project in DIR\n"
    "  --new-project DIR      make a project in DIR (its name is the folder's) and open it\n"
    "  --template NAME        the first level of --new-project: basic, default, combat or parkour (basic)\n"
    "  --disc PATH            the player's disc, for Run in Coney to play the level (else CONEY_DISC)\n"
    "  --select N             select entity N of the open level\n"
    "  --frame                glide the Level view's camera to frame the selection, as F does\n"
    "  --view NAME            put the Level view's camera at the level's camera view NAME\n"
    "  --frames N             run N frames, then quit (lockstep, for tests and screenshots)\n"
    "  --screenshot PNG       save the last frame of --frames as a PNG\n"
    "  --window-size WxH      the window's size in pixels (1600x900)\n"
    "  --no-activate          do not take the keyboard focus when the window opens\n"
    "  --help                 this text\n";

// What the command line asks for.
struct StudioOptions {
    std::optional<std::filesystem::path> project;
    std::optional<std::filesystem::path> newProject;
    std::string levelTemplate{"basic"};
    std::optional<std::string> disc;
    std::optional<std::uint32_t> select;
    std::optional<std::string> view;
    std::optional<int> frames;
    std::optional<std::string> screenshot;
    int width = 1600;
    int height = 900;
    bool activate = true;
    bool frame = false;
    bool help = false;
};

// `text` as a whole number, or nothing.
std::optional<int> number(std::string_view text) {
    int value = 0;
    const auto [at, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && at == text.data() + text.size() ? std::optional<int>(value) : std::nullopt;
}

// Reads the command line; an error message for a bad one.
std::expected<StudioOptions, std::string> parse(const std::vector<std::string_view>& args) {
    StudioOptions options;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        const auto value = [&]() -> std::optional<std::string_view> {
            return i + 1 < args.size() ? std::optional<std::string_view>(args[++i]) : std::nullopt;
        };
        if (arg == "--help") {
            options.help = true;
        } else if (arg == "--frame") {
            options.frame = true;
        } else if (arg == "--no-activate") {
            options.activate = false;
        } else if (arg == "--project" || arg == "--new-project" || arg == "--template" || arg == "--disc" ||
                   arg == "--select" || arg == "--view" || arg == "--frames" || arg == "--screenshot" ||
                   arg == "--window-size") {
            const auto given = value();
            if (!given) {
                return std::unexpected(std::format("{} needs a value", arg));
            }
            if (arg == "--project") {
                options.project = std::filesystem::path(*given);
            } else if (arg == "--new-project") {
                options.newProject = std::filesystem::path(*given);
            } else if (arg == "--template") {
                options.levelTemplate = std::string(*given);
            } else if (arg == "--disc") {
                options.disc = std::string(*given);
            } else if (arg == "--view") {
                options.view = std::string(*given);
            } else if (arg == "--screenshot") {
                options.screenshot = std::string(*given);
            } else if (arg == "--select") {
                const auto n = number(*given);
                if (!n || *n < 0) {
                    return std::unexpected("--select needs an entity number");
                }
                options.select = static_cast<std::uint32_t>(*n);
            } else if (arg == "--frames") {
                options.frames = number(*given);
                if (!options.frames || *options.frames < 1) {
                    return std::unexpected("--frames needs a whole number of at least 1");
                }
            } else {
                const auto x = given->find('x');
                const auto w = number(given->substr(0, x));
                const auto h = x == std::string_view::npos ? std::nullopt : number(given->substr(x + 1));
                if (!w || !h || *w < 320 || *h < 240) {
                    return std::unexpected("--window-size needs WxH, at least 320x240");
                }
                options.width = *w;
                options.height = *h;
            }
        } else {
            return std::unexpected(std::format("unknown option {}", arg));
        }
    }
    if (options.screenshot && !options.frames) {
        return std::unexpected("--screenshot needs --frames: it saves the last frame");
    }
    if (options.project && options.newProject) {
        return std::unexpected("--project and --new-project cannot be combined");
    }
    return options;
}

// The folder of the running executable, where `coney`, its assets and Studio's fonts are.
std::filesystem::path executableFolder() {
    const char* base = SDL_GetBasePath();
    return base != nullptr ? std::filesystem::path(reinterpret_cast<const char8_t*>(base)) : std::filesystem::path();
}

// Adds the font at `path` to the atlas at `size`, or returns null (Dear ImGui's default stays) when it is missing.
ImFont* addFont(const std::filesystem::path& path, float size) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) {
        return nullptr;
    }
    return ImGui::GetIO().Fonts->AddFontFromFileTTF(path.string().c_str(), size);
}

// Where Studio keeps the user's layout: Dear ImGui's settings in the per-user data folder, never in a project
// (docs/guides/studio/editor-experience.md#layouts-coney).
std::string layoutFile() {
    char* folder = SDL_GetPrefPath("Coney", "Coney");
    if (folder == nullptr) {
        return {};
    }
    std::string path = std::string(folder) + "coney-studio-layout.ini";
    SDL_free(folder);
    return path;
}

} // namespace

int main(int argc, char** argv) {
    coney::platform::reportErrorsToConsole();
    // 1. The command line.
    const std::vector<std::string_view> args(argv + 1, argv + argc);
    auto parsed = parse(args);
    if (!parsed) {
        std::fprintf(stderr, "coney-studio: %s\n\n%.*s", parsed.error().c_str(), static_cast<int>(kUsage.size()),
                     kUsage.data());
        return 2;
    }
    const StudioOptions& options = *parsed;
    if (options.help) {
        std::fwrite(kUsage.data(), 1, kUsage.size(), stdout);
        return 0;
    }
    const bool testMode = options.frames.has_value();

    // 2. The window and its OpenGL context, made by librw as the game's are, resizable.
    const coney::platform::WindowDesc desc{.title = "Coney Studio",
                                           .width = options.width,
                                           .height = options.height,
                                           .activate = options.activate && !testMode,
                                           .display = {}};
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::OpenGl, desc);
    if (!engine) {
        std::fprintf(stderr, "coney-studio: %s\n", engine.error().message.c_str());
        // A test run on a machine with no display or OpenGL 3.3 (CI's runners) is skipped, as ctest reads 77.
        return testMode ? 77 : 1;
    }
    const auto [window, context] = (*engine)->sdlWindowAndContext();
    auto* sdlWindow = static_cast<SDL_Window*>(window);
    SDL_SetWindowResizable(sdlWindow, true);
    (*engine)->setVsync(!testMode);

    // 3. Dear ImGui with docking, the layout file per user (none in a test run), the fonts at the display's scale.
    const float scale = std::max(1.0F, SDL_GetWindowDisplayScale(sdlWindow));
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    const std::string iniPath = testMode ? std::string() : layoutFile();
    io.IniFilename = iniPath.empty() ? nullptr : iniPath.c_str();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
    if (testMode) {
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    }
    const std::filesystem::path here = executableFolder();
    const coney::studio::ThemeTokens tokens;
    ImFont* interfaceFont = addFont(here / "fonts" / tokens.interfaceFont, tokens.fontSize * scale);
    if (interfaceFont == nullptr) {
        io.Fonts->AddFontDefault();
    }
    ImFont* boldFont = addFont(here / "fonts" / tokens.interfaceBoldFont, tokens.fontSize * scale);
    ImFont* codeFont = addFont(here / "fonts" / tokens.codeFont, tokens.codeFontSize * scale);
    if (!ImGui_ImplSDL3_InitForOpenGL(sdlWindow, context) || !ImGui_ImplOpenGL3_Init(nullptr)) {
        std::fprintf(stderr, "coney-studio: Dear ImGui's SDL3 or OpenGL 3 backend did not start\n");
        ImGui::DestroyContext();
        return 1;
    }

    // 4. The editor, its host, and the run log's lines in its Console as well as on the terminal.
    int status = 0;
    {
        coney::platform::SdlStudioHost host(sdlWindow, **engine);
        coney::studio::StudioSettings settings{.assets = here / "assets",
                                               .coney = coney::studio::coneyBeside(here / "coney-studio.exe"),
                                               .disc = options.disc,
                                               .theme = tokens,
                                               .uiScale = scale};
#ifndef _WIN32
        settings.coney = here / "coney";
#endif
        if (!settings.disc) {
            if (const char* disc = SDL_getenv("CONEY_DISC"); disc != nullptr && disc[0] != '\0') {
                settings.disc = std::string(disc);
            }
        }
        coney::studio::StudioApp app(host, settings);
        app.setFonts(interfaceFont, boldFont, codeFont);
        coney::logging::setSink([&app](const coney::logging::Record& record, std::string_view line) {
            std::fprintf(stdout, "%.*s\n", static_cast<int>(line.size()), line.data());
            const auto level = record.level == coney::logging::Level::Error  ? ConsoleLine::Level::Error
                               : record.level == coney::logging::Level::Warn ? ConsoleLine::Level::Warn
                                                                             : ConsoleLine::Level::Info;
            app.log(level, record.text);
        });
        const auto reportFailure = [&app, &status](std::string_view what, const coney::Error& error) {
            app.log(ConsoleLine::Level::Error, std::format("could not {}: {}", what, error.message));
            std::fprintf(stderr, "coney-studio: could not %.*s: %s\n", static_cast<int>(what.size()), what.data(),
                         error.message.c_str());
            status = 1;
        };
        if (options.newProject) {
            const auto name = options.newProject->filename().string();
            if (auto made = app.newProject(*options.newProject, name, options.levelTemplate); !made) {
                reportFailure("make the project", made.error());
            }
        } else if (options.project) {
            if (auto opened = app.openProject(*options.project); !opened) {
                reportFailure("open the project", opened.error());
            }
        }
        if (options.select) {
            app.select(options.select);
        }
        if (options.view && !app.goToView(*options.view)) {
            app.log(ConsoleLine::Level::Warn, std::format("the level has no camera view called {}", *options.view));
        }
        if (options.frame) {
            app.frameSelection();
        }

        // 5. The main loop: events, the frame's camera steps, the interface, the level drawn into its texture inside
        // it, then the window cleared and Dear ImGui drawn over it. A test run is lockstep: one step a frame, no clock.
        coney::FrameClock clock(testMode ? coney::FramePacing::Lockstep : coney::FramePacing::Interpolated);
        std::optional<coney::platform::FramePacer> pacer;
        if (!testMode) {
            pacer.emplace(0);
        }
        // The event window of an OpenGL engine always exists; without it the loop would end at once.
        std::optional<coney::platform::Window> events = (*engine)->window();
        (*engine)->setPresentOverlay(
            [](coney::graphics::RenderDevice&) { ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData()); });
        const int frames = options.frames.value_or(0);
        if (options.screenshot) {
            (*engine)->requestCapture(static_cast<std::uint64_t>(frames - 1), *options.screenshot);
        }
        std::string title;
        for (int frame = 0; !testMode || frame < frames; ++frame) {
            const bool running = events.has_value() && events->pumpEvents([](const void* event) {
                ImGui_ImplSDL3_ProcessEvent(static_cast<const SDL_Event*>(event));
                return true; // Escape belongs to the editor, never quits it
            });
            if (!running || app.quitRequested()) {
                break;
            }
            host.deliver();
            const coney::FramePlan plan = clock.advance(pacer ? pacer->waitForFrame() : 0);
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            if (testMode) {
                io.DeltaTime = 1.0F / 30.0F;
            }
            ImGui::NewFrame();
            app.frame(plan.steps, 1.0F / 30.0F, plan.alpha);
            ImGui::Render();
            if (const std::string error = host.takeDrawError(); !error.empty()) {
                app.log(ConsoleLine::Level::Error, "Level view: " + error);
            }
            // The window is cleared to the dark background the panels sit on.
            const coney::studio::ThemeColour base = coney::studio::derivePalette(tokens).dark2;
            (*engine)->beginWindowFrame(coney::graphics::Rgba{static_cast<std::uint8_t>(base.r * 255.0F),
                                                              static_cast<std::uint8_t>(base.g * 255.0F),
                                                              static_cast<std::uint8_t>(base.b * 255.0F), 255});
            (*engine)->present();
            if (pacer) {
                pacer->endFrame(plan.steps);
            }
            if (const std::string now = app.windowTitle(); now != title) {
                title = now;
                SDL_SetWindowTitle(sdlWindow, title.c_str());
            }
        }
        if (options.screenshot) {
            const auto& capture = (*engine)->capture();
            if (!capture || !*capture) {
                std::fprintf(stderr, "coney-studio: no screenshot: %s\n",
                             capture ? capture->error().message.c_str() : "the frame was not reached");
                status = 1;
            } else {
                std::fprintf(stdout, "screenshot: %s (%dx%d)\n", (*capture)->path.c_str(), (*capture)->size.width,
                             (*capture)->size.height);
            }
        }
        if (testMode) {
            const auto& project = app.project();
            const LevelDocument* level = app.level();
            const std::optional<EntityId> selection = app.selection();
            const std::string selected = selection ? std::to_string(*selection) : "none";
            std::fprintf(stdout, "studio: %d frames, project %s, level %s, %zu entities, selection %s\n", frames,
                         project ? project->manifest().name.c_str() : "none",
                         level != nullptr ? level->title().c_str() : "none",
                         level != nullptr ? level->entities().size() : std::size_t{0}, selected.c_str());
        }
        (*engine)->setPresentOverlay({});
        coney::logging::setSink({});
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    return status;
}
