// SPDX-License-Identifier: GPL-3.0-or-later
// The F8 bar's widgets over a bare Dear ImGui context, with no window and no renderer: what it draws open, closed and
// pinned, the line typed into with completion and Enter, the pad's picker, and the pinned terminal kept in the
// settings (docs/guides/console.md#front-end, docs/guides/studio/console.md#tests).
#include "platform/console_view.h"

#include <expected>
#include <string>
#include <string_view>

#include <SDL3/SDL.h>
#include <catch2/catch_test_macros.hpp>
#include <imgui.h>
#include <imgui_internal.h> // FindWindowByName

#include "console/command_spec.h"
#include "console/console.h"
#include "console/console_pad.h"
#include "console/registry.h"
#include "platform/console_ui.h"

using coney::console::CommandContext;
using coney::console::CommandSpec;
using coney::console::Console;
using coney::console::ConsolePad;
using coney::console::OpenedBy;
using coney::platform::ConsoleEventRoute;
using coney::platform::consoleUiSettings;
using coney::platform::ConsoleView;
using coney::platform::routeConsoleEvent;
using Action = coney::platform::ConsoleEventRoute::Action;

namespace {

// A fresh context with no backend, a console with `help`, `here` and `where`, and the view over them. The atlas is
// left to a renderer that "has textures", so no font upload is needed.
class BareConsole {
  public:
    BareConsole() {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(1280.0F, 720.0F);
        io.DeltaTime = 1.0F / 30.0F;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        const auto prints = [](std::string_view name) {
            return [name](CommandContext& context) -> std::expected<void, std::string> {
                context.print(std::string(name) + " ran");
                return {};
            };
        };
        auto& registry = console.registry();
        REQUIRE(
            registry.addBuiltin(CommandSpec{.name = "help", .group = "General", .summary = "Lists."}, prints("help")));
        REQUIRE(
            registry.addBuiltin(CommandSpec{.name = "here", .group = "General", .summary = "Reads."}, prints("here")));
        REQUIRE(registry.addBuiltin(CommandSpec{.name = "where", .group = "General", .summary = "Reads."},
                                    prints("where")));
        view.addSettingsHandler();
    }
    ~BareConsole() { ImGui::DestroyContext(); }
    BareConsole(const BareConsole&) = delete;
    BareConsole& operator=(const BareConsole&) = delete;
    BareConsole(BareConsole&&) = delete;
    BareConsole& operator=(BareConsole&&) = delete;

    // One whole frame of the view.
    void frame() {
        ImGui::NewFrame();
        view.draw();
        ImGui::Render();
    }
    // A key pressed in one frame and let go in the next.
    void key(ImGuiKey key) {
        ImGui::GetIO().AddKeyEvent(key, true);
        frame();
        ImGui::GetIO().AddKeyEvent(key, false);
        frame();
    }
    // Opens the console from the keyboard, as F8 does, and lets the line take the focus.
    void openFromKeyboard() {
        pad.setOpen(true, OpenedBy::Keyboard);
        view.focusLine();
        frame();
        frame();
    }

    Console console{{}, {}};
    ConsolePad pad{nullptr, console};
    ConsoleView view{console, pad};
};

// The window named `name` if it was drawn in the last frame (Active stays set from its Begin to the next NewFrame).
ImGuiWindow* drawn(const char* name) {
    ImGuiWindow* window = ImGui::FindWindowByName(name);
    return window != nullptr && window->Active ? window : nullptr;
}

// A key press of `key`, a repeat of a held one when `repeat`.
SDL_Event keyDown(SDL_Keycode key, bool repeat = false) {
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = key;
    event.key.repeat = repeat;
    return event;
}

// An event of `type` with nothing else set.
SDL_Event plain(SDL_EventType type) {
    SDL_Event event{};
    event.type = type;
    return event;
}
} // namespace

TEST_CASE("closed and with nothing pinned, the bar draws nothing", "[platform][console][front-end]") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#test-mode
    BareConsole bare;
    bare.frame();
    CHECK(drawn(ConsoleView::kTerminalWindow) == nullptr);
    CHECK(ImGui::GetDrawData()->CmdLists.Size == 0);
}

TEST_CASE("open, the bar draws its menu bar and the terminal", "[platform][console][front-end]") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#input
    BareConsole bare;
    bare.openFromKeyboard();
    CHECK(drawn(ConsoleView::kTerminalWindow) != nullptr);
    CHECK(drawn(ConsoleView::kPickerWindow) == nullptr);
    CHECK(ImGui::GetDrawData()->CmdLists.Size > 0);
    // Closed again, it is gone.
    bare.pad.setOpen(false);
    bare.frame();
    CHECK(drawn(ConsoleView::kTerminalWindow) == nullptr);
}

TEST_CASE("the line takes typed text, completes it with Tab and runs it with Enter", "[platform][console][front-end]") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#terminal
    BareConsole bare;
    bare.openFromKeyboard();
    ImGui::GetIO().AddInputCharactersUTF8("wh");
    bare.frame();
    CHECK(bare.view.editor().text() == "wh");
    bare.key(ImGuiKey_Tab);
    CHECK(bare.view.editor().text() == "where ");
    bare.key(ImGuiKey_Enter);
    CHECK(bare.view.editor().text().empty());
    const auto& output = bare.console.terminal().output();
    REQUIRE(output.size() == 2);
    CHECK(output[0].text == "> where");
    CHECK(output[1].text == "where ran");
    // Up brings the line back from the history, without the space Tab left.
    bare.key(ImGuiKey_UpArrow);
    CHECK(bare.view.editor().text() == "where");
}

TEST_CASE("opened from the pad, the bar shows the command picker", "[platform][console][front-end]") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#input
    BareConsole bare;
    bare.pad.setOpen(true, OpenedBy::Pad);
    bare.frame();
    CHECK(drawn(ConsoleView::kPickerWindow) != nullptr);
    CHECK(drawn(ConsoleView::kTerminalWindow) != nullptr);
}

TEST_CASE("a pinned terminal draws with the console closed and takes no input", "[platform][console][front-end]") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#tests
    BareConsole bare;
    bare.view.setPinned(true);
    bare.frame();
    const ImGuiWindow* terminal = drawn(ConsoleView::kTerminalWindow);
    REQUIRE(terminal != nullptr);
    CHECK((terminal->Flags & ImGuiWindowFlags_NoInputs) != 0);
    ImGui::GetIO().AddInputCharactersUTF8("help");
    bare.frame();
    bare.key(ImGuiKey_Enter);
    CHECK(bare.view.editor().text().empty());
    CHECK(bare.console.terminal().output().empty());
}

TEST_CASE("the pinned terminal is kept in the settings", "[platform][console][front-end]") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#test-mode
    std::string saved;
    {
        BareConsole bare;
        bare.view.setPinned(true);
        bare.frame();
        saved = ImGui::SaveIniSettingsToMemory();
    }
    CHECK(saved.find("[ConeyConsole][Panels]") != std::string::npos);
    CHECK(saved.find("Pinned=1") != std::string::npos);
    BareConsole again;
    CHECK_FALSE(again.view.pinned());
    ImGui::LoadIniSettingsFromMemory(saved.c_str(), saved.size());
    CHECK(again.view.pinned());
}

// The bar's event routing and settings, decided apart from Dear ImGui and the window (ConsoleUi uses them).
TEST_CASE("F8 toggles the bar outside test mode, and a test-mode run does not catch it",
          "[platform][console][front-end]") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#test-mode
    const SDL_Event f8 = keyDown(SDLK_F8);
    const ConsoleEventRoute closed = routeConsoleEvent(&f8, false, false);
    CHECK(closed.action == Action::Toggle);
    CHECK_FALSE(closed.forward);
    CHECK(routeConsoleEvent(&f8, true, false).action == Action::Toggle);
    // Test mode reads no device: F8 is the game's, and a closed bar shows Dear ImGui nothing.
    const ConsoleEventRoute test = routeConsoleEvent(&f8, false, true);
    CHECK(test.action == Action::None);
    CHECK_FALSE(test.forward);
    CHECK_FALSE(test.takesKeyboard);
    // A held F8 repeating is no second press.
    const SDL_Event repeat = keyDown(SDLK_F8, true);
    CHECK(routeConsoleEvent(&repeat, false, false).action == Action::None);
}

TEST_CASE("Escape closes the open bar and is left alone when it is closed", "[platform][console][front-end]") {
    // Expected: Coney's own rule, docs/guides/console.md#front-end
    const SDL_Event escape = keyDown(SDLK_ESCAPE);
    const ConsoleEventRoute open = routeConsoleEvent(&escape, true, false);
    CHECK(open.action == Action::Close);
    CHECK_FALSE(open.forward);
    const ConsoleEventRoute closed = routeConsoleEvent(&escape, false, false);
    CHECK(closed.action == Action::None);
    CHECK_FALSE(closed.forward);
    CHECK_FALSE(closed.takesKeyboard);
}

TEST_CASE("a closed bar shows Dear ImGui no event, so its queue cannot fill", "[platform][console][front-end]") {
    // Expected: Coney's own rule, docs/guides/console.md#front-end
    const SDL_Event events[] = {keyDown(SDLK_A),
                                keyDown(SDLK_UP),
                                plain(SDL_EVENT_KEY_UP),
                                plain(SDL_EVENT_TEXT_INPUT),
                                plain(SDL_EVENT_MOUSE_MOTION),
                                plain(SDL_EVENT_MOUSE_BUTTON_DOWN),
                                plain(SDL_EVENT_WINDOW_FOCUS_LOST)};
    for (const SDL_Event& event : events) {
        const ConsoleEventRoute route = routeConsoleEvent(&event, false, false);
        CHECK_FALSE(route.forward);
        CHECK_FALSE(route.takesKeyboard);
        CHECK(route.action == Action::None);
    }
}

TEST_CASE("an open bar shows Dear ImGui every event, takes the keyboard, and the mouse when over its windows",
          "[platform][console][front-end]") {
    // Expected: Coney's own rule, docs/guides/console.md#front-end
    const SDL_Event key = keyDown(SDLK_A);
    const ConsoleEventRoute typed = routeConsoleEvent(&key, true, false);
    CHECK(typed.forward);
    CHECK(typed.takesKeyboard);
    CHECK_FALSE(typed.mouse);
    const SDL_Event text = plain(SDL_EVENT_TEXT_INPUT);
    CHECK(routeConsoleEvent(&text, true, false).takesKeyboard);
    const SDL_Event motion = plain(SDL_EVENT_MOUSE_MOTION);
    const ConsoleEventRoute moved = routeConsoleEvent(&motion, true, false);
    CHECK(moved.forward);
    CHECK(moved.mouse);
    CHECK_FALSE(moved.takesKeyboard);
    // --console-open in a test-mode run opens it without F8; its keys are the bar's all the same.
    CHECK(routeConsoleEvent(&key, true, true).takesKeyboard);
}

TEST_CASE("a test-mode run asks for no settings file, so no user folder is made", "[platform][console][front-end]") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#test-mode
    const coney::platform::ConsoleUiSettings test = consoleUiSettings(true);
    CHECK(test.testMode);
    CHECK(test.iniPath.empty());
}
