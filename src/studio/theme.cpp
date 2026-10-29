// SPDX-License-Identifier: GPL-3.0-or-later
#include "studio/theme.h"

#include <algorithm>

#include <imgui.h>

namespace coney::studio {

namespace {

// Dear ImGui's colour from ours.
ImVec4 toImGui(ThemeColour c) { return ImVec4(c.r, c.g, c.b, c.a); }

// `c` with alpha `a`.
ThemeColour withAlpha(ThemeColour c, float a) {
    c.a = a;
    return c;
}

} // namespace

ThemeColour hexColour(std::uint32_t rgb) {
    constexpr float kChannel = 255.0F;
    return ThemeColour{.r = static_cast<float>((rgb >> 16U) & 0xFFU) / kChannel,
                       .g = static_cast<float>((rgb >> 8U) & 0xFFU) / kChannel,
                       .b = static_cast<float>(rgb & 0xFFU) / kChannel,
                       .a = 1.0F};
}

ThemeColour textColourFor(ThemeColour base) {
    // Rec. 709 luminance: past the middle the base is light and takes black text.
    const float luminance = (0.2126F * base.r) + (0.7152F * base.g) + (0.0722F * base.b);
    return luminance > 0.5F ? ThemeColour{0.0F, 0.0F, 0.0F, 1.0F} : ThemeColour{1.0F, 1.0F, 1.0F, 1.0F};
}

ThemeColour shade(ThemeColour colour, float amount) {
    const float t = std::clamp(amount, -1.0F, 1.0F);
    const auto move = [t](float channel) {
        return t >= 0.0F ? channel * (1.0F - t) : channel + ((1.0F - channel) * -t);
    };
    return ThemeColour{move(colour.r), move(colour.g), move(colour.b), colour.a};
}

ThemePalette derivePalette(const ThemeTokens& tokens) {
    // Godot's editor theme makes its dark colours by moving the base towards black by the contrast, 1.5 and 2 times
    // it (editor_theme_manager.cpp, tag 4.6-stable), and paints what is chosen with the accent at partial opacity.
    const float c = tokens.contrast;
    const ThemeColour text = textColourFor(tokens.base);
    ThemePalette palette;
    palette.base = tokens.base;
    palette.dark1 = shade(tokens.base, c);
    palette.dark2 = shade(tokens.base, c * 1.5F);
    palette.dark3 = shade(tokens.base, c * 2.0F);
    palette.hover = shade(palette.dark1, -0.12F);
    palette.line = withAlpha(text, 0.10F);
    palette.text = text;
    palette.textDim = withAlpha(text, 0.45F);
    palette.accent = tokens.accent;
    palette.selection = withAlpha(tokens.accent, 0.35F);
    palette.section = withAlpha(tokens.accent, 0.16F);
    return palette;
}

void applyTheme(const ThemeTokens& tokens, float scale, ImGuiStyle& style) {
    // 1. Sizes: Godot's corner radius, borders and base spacing, at the UI scale; the dock's gaps are one base spacing
    // wide so the panels stand apart on the dark background, as Godot's docks do.
    const float radius = tokens.cornerRadius * scale;
    const float space = tokens.spacing * scale;
    style.WindowRounding = radius;
    style.ChildRounding = radius;
    style.FrameRounding = radius;
    style.PopupRounding = radius;
    style.TabRounding = radius;
    style.GrabRounding = radius;
    style.WindowBorderSize = tokens.borderSize * scale;
    style.FrameBorderSize = tokens.borderSize * scale;
    style.PopupBorderSize = std::max(tokens.borderSize, 1.0F) * scale;
    style.TabBorderSize = 0.0F;
    style.TabBarBorderSize = 0.0F;
    style.TabBarOverlineSize = 2.0F * scale;
    style.ChildBorderSize = tokens.borderSize * scale;
    style.DockingSeparatorSize = space;
    style.WindowPadding = ImVec2(space * 2.0F, space * 2.0F);
    style.FramePadding = ImVec2(space * 2.0F, space * 1.25F);
    style.ItemSpacing = ImVec2(space * 2.0F, space * 1.5F);
    style.ItemInnerSpacing = ImVec2(space * 1.5F, space);
    style.CellPadding = ImVec2(space * 1.5F, space * 0.75F);
    style.IndentSpacing = space * 4.0F;
    style.ScrollbarSize = space * 2.5F;
    style.ScrollbarRounding = style.ScrollbarSize * 0.5F;
    style.GrabMinSize = space * 2.5F;
    style.SeparatorTextBorderSize = 1.0F * scale;
    style.WindowMenuButtonPosition = ImGuiDir_None;

    // 2. Colours: the panels are the base; everything behind and between them (menu bar, tab strips, dock gaps) the
    // second dark shade; fields and buttons the first; the accent on what is chosen, active or focused.
    const ThemePalette p = derivePalette(tokens);
    const ThemeColour clear = withAlpha(p.base, 0.0F);
    ImVec4* colours = style.Colors;
    colours[ImGuiCol_Text] = toImGui(p.text);
    colours[ImGuiCol_TextDisabled] = toImGui(p.textDim);
    colours[ImGuiCol_WindowBg] = toImGui(p.base);
    colours[ImGuiCol_ChildBg] = toImGui(clear);
    colours[ImGuiCol_PopupBg] = toImGui(p.base);
    colours[ImGuiCol_Border] = toImGui(p.line);
    colours[ImGuiCol_BorderShadow] = toImGui(clear);
    colours[ImGuiCol_FrameBg] = toImGui(p.dark1);
    colours[ImGuiCol_FrameBgHovered] = toImGui(p.hover);
    colours[ImGuiCol_FrameBgActive] = toImGui(p.dark1);
    colours[ImGuiCol_TitleBg] = toImGui(p.dark2);
    colours[ImGuiCol_TitleBgActive] = toImGui(p.dark2);
    colours[ImGuiCol_TitleBgCollapsed] = toImGui(p.dark2);
    colours[ImGuiCol_MenuBarBg] = toImGui(p.dark2);
    colours[ImGuiCol_ScrollbarBg] = toImGui(p.dark3);
    colours[ImGuiCol_ScrollbarGrab] = toImGui(withAlpha(p.text, 0.14F));
    colours[ImGuiCol_ScrollbarGrabHovered] = toImGui(withAlpha(p.text, 0.24F));
    colours[ImGuiCol_ScrollbarGrabActive] = toImGui(p.accent);
    colours[ImGuiCol_CheckMark] = toImGui(p.accent);
    colours[ImGuiCol_SliderGrab] = toImGui(p.accent);
    colours[ImGuiCol_SliderGrabActive] = toImGui(shade(p.accent, -0.2F));
    colours[ImGuiCol_Button] = toImGui(p.dark1);
    colours[ImGuiCol_ButtonHovered] = toImGui(p.hover);
    colours[ImGuiCol_ButtonActive] = toImGui(withAlpha(p.accent, 0.6F));
    colours[ImGuiCol_Header] = toImGui(p.selection);
    colours[ImGuiCol_HeaderHovered] = toImGui(withAlpha(p.text, 0.07F));
    colours[ImGuiCol_HeaderActive] = toImGui(withAlpha(p.accent, 0.5F));
    colours[ImGuiCol_Separator] = toImGui(p.line);
    colours[ImGuiCol_SeparatorHovered] = toImGui(p.accent);
    colours[ImGuiCol_SeparatorActive] = toImGui(p.accent);
    colours[ImGuiCol_ResizeGrip] = toImGui(clear);
    colours[ImGuiCol_ResizeGripHovered] = toImGui(p.accent);
    colours[ImGuiCol_ResizeGripActive] = toImGui(p.accent);
    colours[ImGuiCol_InputTextCursor] = toImGui(p.text);
    colours[ImGuiCol_Tab] = toImGui(p.dark2);
    colours[ImGuiCol_TabHovered] = toImGui(p.hover);
    colours[ImGuiCol_TabSelected] = toImGui(p.base);
    colours[ImGuiCol_TabSelectedOverline] = toImGui(p.accent);
    colours[ImGuiCol_TabDimmed] = toImGui(p.dark2);
    colours[ImGuiCol_TabDimmedSelected] = toImGui(p.base);
    colours[ImGuiCol_TabDimmedSelectedOverline] = toImGui(withAlpha(p.accent, 0.45F));
    colours[ImGuiCol_DockingPreview] = toImGui(withAlpha(p.accent, 0.5F));
    colours[ImGuiCol_DockingEmptyBg] = toImGui(p.dark2);
    colours[ImGuiCol_TableHeaderBg] = toImGui(p.dark1);
    colours[ImGuiCol_TableBorderStrong] = toImGui(p.line);
    colours[ImGuiCol_TableBorderLight] = toImGui(withAlpha(p.text, 0.05F));
    colours[ImGuiCol_TextSelectedBg] = toImGui(p.selection);
    colours[ImGuiCol_TextLink] = toImGui(p.accent);
    colours[ImGuiCol_DragDropTarget] = toImGui(p.accent);
    colours[ImGuiCol_NavCursor] = toImGui(p.accent);
    colours[ImGuiCol_NavWindowingHighlight] = toImGui(p.accent);
    colours[ImGuiCol_ModalWindowDimBg] = toImGui(ThemeColour{0.0F, 0.0F, 0.0F, 0.5F});
}

} // namespace coney::studio
