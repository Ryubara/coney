// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string>

#include "console/console.h"
#include "console/console_pad.h"
#include "console/line_editor.h"

// Dear ImGui's settings-file hook and its context, declared in its headers.
struct ImGuiContext;
struct ImGuiInputTextCallbackData;
struct ImGuiSettingsHandler;
struct ImGuiTextBuffer;

// @coney-only the Coney console's F8 bar (docs/guides/console.md#front-end); the original has no console.

namespace coney::platform {

/// The F8 bar's widgets, drawn with Dear ImGui inside a frame someone else begins (docs/guides/console.md#front-end):
/// the menu bar across the top, the terminal (its output above the line, the candidates while typing) and, when the
/// pad opened the console, the command picker. Closed, it draws nothing, unless the terminal is pinned: then its output
/// stays on screen and takes no input. It draws no backend and reads no device, so the tests run it on a bare
/// context; ConsoleUi gives it the window.
class ConsoleView {
  public:
    /// The terminal's window name, and the picker's.
    static constexpr const char* kTerminalWindow = "Terminal###coney-console-terminal";
    static constexpr const char* kPickerWindow = "Commands###coney-console-picker";
    /// The bar's title. (The release check `coney.release.has_console_bar` keys on `kTerminalWindow`'s id,
    /// `coney-console-terminal`: renaming it means changing `cmake/console_bar_marker.cmake`.)
    static constexpr const char* kTitle = "Coney console";
    /// How many candidates show above the line at most.
    static constexpr std::size_t kCandidatesShown = 8;

    /// A view of `console`, opened and closed through `pad`; both must outlive it.
    ConsoleView(console::Console& console, console::ConsolePad& pad)
        : m_pad(pad), m_editor(console), m_console(console) {}

    /// Registers the settings hook that keeps the pinned terminal in the settings file (`[ConeyConsole][Panels]`).
    /// Call once on the view's own context, before its settings are loaded; the view must outlive the context.
    void addSettingsHandler();

    /// Draws the bar and its windows in the current frame, as described above.
    void draw();

    /// Whether the terminal stays on screen with the console closed.
    [[nodiscard]] bool pinned() const { return m_pinned; }
    /// Pins or unpins the terminal, and marks the settings to be saved.
    void setPinned(bool pinned);
    /// Gives the line the keyboard at the next draw: the console was opened from the keyboard.
    void focusLine() { m_focusLine = true; }
    /// The line.
    [[nodiscard]] console::LineEditor& editor() { return m_editor; }

  private:
    // The menu bar.
    void drawBar();
    // The terminal window: `interactive` with its line, or pinned with the console closed.
    void drawTerminal(bool interactive);
    // The line, its candidates above it.
    void drawLine();
    // The command picker.
    void drawPicker();
    // Dear ImGui's text box callback: Tab and Up and Down through the editor.
    static int lineCallback(ImGuiInputTextCallbackData* data);
    // The settings file's hooks.
    static void* openSettings(ImGuiContext* context, ImGuiSettingsHandler* handler, const char* name);
    static void readSettingsLine(ImGuiContext* context, ImGuiSettingsHandler* handler, void* entry, const char* line);
    static void writeSettings(ImGuiContext* context, ImGuiSettingsHandler* handler, ImGuiTextBuffer* out);

    console::ConsolePad& m_pad;
    console::LineEditor m_editor;
    console::Console& m_console;
    std::string m_buffer;         // the text box's copy of the line
    std::size_t m_outputSeen = 0; // output lines when last drawn, to follow new ones
    bool m_pinned = false;
    bool m_focusLine = false;
};

} // namespace coney::platform
