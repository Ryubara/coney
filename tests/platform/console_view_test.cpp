// SPDX-License-Identifier: GPL-3.0-or-later
// The F8 bar's widgets over a bare Dear ImGui context, with no window and no renderer: what it draws open, closed and
// pinned, the line typed into with completion and Enter, the pad's picker, and the pinned terminal kept in the
// settings (docs/guides/console.md#front-end, docs/guides/studio/console.md#tests).
#include "platform/console_view.h"

#include <expected>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>
#include <imgui.h>
#include <imgui_internal.h> // FindWindowByName

#include "console/command_spec.h"
#include "console/console.h"
#include "console/console_pad.h"
#include "console/registry.h"

using coney::console::CommandContext;
using coney::console::CommandSpec;
using coney::console::Console;
using coney::console::ConsolePad;
using coney::console::OpenedBy;
using coney::platform::ConsoleView;

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
