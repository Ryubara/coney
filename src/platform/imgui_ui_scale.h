// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <imgui.h>

#include "debug/debug_session.h"

// Dear ImGui's settings-file hook, declared in its internal header.
struct ImGuiSettingsHandler;

namespace coney::platform {

/// Keeps the developer overlay the same physical size at any resolution (docs/guides/debug-menu.md#ui-scale): it picks
/// the scale (debug::uiScaleFor(): automatic from the display's height, or the Display page's override), then sets
/// the style's sizes and the font size to it. The font is not stretched: at 1x it is Dear ImGui's pixel-clean 13 px
/// font, above that its scalable font at 13 px times the scale, so Dear ImGui's dynamic atlas draws the glyphs again
/// at the new size and the text stays sharp.
///
/// A change of scale (a window resized, moved to another display, or a new override) also resizes and moves the open
/// windows by the same ratio, so their contents still fit and the layout keeps its shape. The scale at which the windows
/// were last saved is kept in the settings file (`[ConeyUi][Scale]`), with the Display page's choice, so windows saved
/// at one scale open at the right size and place at another.
///
/// Needs a current ImGui context; does no drawing, so tests run it on a bare context.
class ImGuiUiScale {
  public:
    /// A scaler over `display` (which holds the Display page's choice, and which it tells the scale it applied); the
    /// display must outlive it.
    explicit ImGuiUiScale(debug::DisplayOptions& display) : m_display(display) {}

    /// Takes the style as it is now as the 1x style, adds the two fonts and registers the settings hook. Call once,
    /// right after the context is made and its colours chosen, before the settings are loaded and before the first
    /// frame; the scaler must outlive the context.
    void setup();

    /// Sets the style and the font for a display `displayHeight` tall (ImGui's DisplaySize.y), when the
    /// scale it gives differs from the one applied; resizes and moves the open windows by the ratio. Call between
    /// frames, before ImGui::NewFrame(). Returns whether the scale changed.
    bool update(float displayHeight);

    /// The scale applied now; 1 before the first update().
    [[nodiscard]] float scale() const { return m_applied > 0.0F ? m_applied : 1.0F; }

  private:
    // The settings file's hooks: the `Scale` entry, its lines, the end of a read, and writing it.
    static void* openSettings(ImGuiContext* context, ImGuiSettingsHandler* handler, const char* name);
    static void readSettingsLine(ImGuiContext* context, ImGuiSettingsHandler* handler, void* entry, const char* line);
    static void applySettings(ImGuiContext* context, ImGuiSettingsHandler* handler);
    static void writeSettings(ImGuiContext* context, ImGuiSettingsHandler* handler, ImGuiTextBuffer* out);

    // Sets the style and the default font for `scale`.
    void applyStyle(float scale);
    // Resizes and moves the windows saved in the settings from the scale they were saved at to the one applied.
    void scaleSavedWindows();
    // Resizes and moves the open windows by `ratio`.
    static void scaleOpenWindows(float ratio);

    debug::DisplayOptions& m_display;
    ImGuiStyle m_baseStyle;             // the style at 1x, which every scale is made from
    ImFont* m_bitmapFont = nullptr;     // the pixel-clean font, used at exactly 1x
    ImFont* m_vectorFont = nullptr;     // the scalable font, used above 1x
    float m_applied = 0.0F;             // the scale applied; 0 before the first update()
    float m_savedScale = 1.0F;          // the scale the settings file's window sizes were saved at
    bool m_savedWindowsPending = false; // the settings were read and their window sizes are not yet scaled
};

} // namespace coney::platform
