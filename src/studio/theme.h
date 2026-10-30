// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

struct ImGuiStyle;

// Coney Studio's theme: a few design tokens from which every colour and size of Dear ImGui's style is derived, as
// Godot's editor derives its theme (docs/guides/studio/editor-experience.md#theme). The defaults are Godot 4's
// *Modern* style with its *Default* preset. Coney's own; the original has no editor.

namespace coney::studio {

/// A colour as four channels from 0 to 1.
struct ThemeColour {
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
    float a = 1.0F;

    friend bool operator==(const ThemeColour&, const ThemeColour&) = default;
};

/// The colour of the hex triplet `rgb` (0x292929), opaque.
[[nodiscard]] ThemeColour hexColour(std::uint32_t rgb);

/// The tokens the user can change (docs/guides/studio/editor-experience.md#theme-defaults).
struct ThemeTokens {
    ThemeColour base = hexColour(0x292929);             ///< Every surface is derived from it.
    ThemeColour accent = hexColour(0x569EFF);           ///< What is active, hovered or chosen.
    float contrast = 0.3F;                              ///< How far the derived surfaces move from the base, -1 to 1.
    float cornerRadius = 4.0F;                          ///< Pixels at UI scale 1.
    float borderSize = 0.0F;                            ///< Pixels.
    float spacing = 4.0F;                               ///< The base spacing, pixels at UI scale 1.
    float fontSize = 14.0F;                             ///< The interface font's size, pixels at UI scale 1.
    float codeFontSize = 14.0F;                         ///< The code font's size, pixels at UI scale 1.
    std::string interfaceFont = "Inter-Regular.ttf";    ///< The interface font, a file in Studio's `fonts/` folder.
    std::string interfaceBoldFont = "Inter-Bold.ttf";   ///< Its bold face, for headers.
    std::string codeFont = "JetBrainsMono-Regular.ttf"; ///< The code font (the Console, later the script editor).
};

/// The colours every panel is painted with, derived from the tokens the way Godot's editor derives its theme
/// (docs/guides/studio/editor-experience.md#theme-imgui): darker shades of the base behind and between the panels, the
/// accent on whatever is chosen, active or focused.
struct ThemePalette {
    ThemeColour base;      ///< The panels' background.
    ThemeColour dark1;     ///< Fields and buttons: the base `contrast` of the way to black.
    ThemeColour dark2;     ///< Behind and between the panels (menu bar, toolbar, tab strips, dock gaps): 1.5 x.
    ThemeColour dark3;     ///< The deepest wells (scrollbar tracks): 2 x.
    ThemeColour hover;     ///< A field or button under the mouse.
    ThemeColour line;      ///< Separators and popup borders: the text colour, faint.
    ThemeColour text;      ///< Text, white or black by the base.
    ThemeColour textDim;   ///< Disabled and secondary text.
    ThemeColour accent;    ///< Toggles, sliders, the active tab's line, focus, a pressed button.
    ThemeColour selection; ///< A chosen row (Hierarchy, File system): the accent, translucent.
    ThemeColour section;   ///< A section header (the Inspector's groups): the accent, fainter than a selection.
};

/// The palette `tokens` give.
[[nodiscard]] ThemePalette derivePalette(const ThemeTokens& tokens);

/// The text colour for `base`: white on a dark base, black on a light one, by its luminance, as Godot picks its mono
/// colour.
[[nodiscard]] ThemeColour textColourFor(ThemeColour base);

/// `colour` moved `amount` of the way towards black (positive) or white (negative), its alpha kept: how the derived
/// surfaces are made from the base and the contrast.
[[nodiscard]] ThemeColour shade(ThemeColour colour, float amount);

/// Fills `style` from `tokens` at UI scale `scale`: the backgrounds from the base, the frames, tabs and title bars
/// shaded by multiples of the contrast, the active and chosen colours from the accent, the text from textColourFor(),
/// the rounding, borders and spacing from the sizes times `scale`.
void applyTheme(const ThemeTokens& tokens, float scale, ImGuiStyle& style);

} // namespace coney::studio
