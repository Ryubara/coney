// SPDX-License-Identifier: GPL-3.0-or-later
// Coney Studio's Level view panel: the sandbox layout drawn into a texture, the markers over it, the fly camera's input
// and the click that selects (docs/guides/studio/prototype.md). The rest of the interface is studio_panels.cpp's.
// @coney-only the editor; the original has none.
#include <array>
#include <format>
#include <optional>
#include <string>

#include <imgui.h>
#include <imgui_internal.h> // IsMouseDragPastThreshold

#include "platform/imgui_model_view.h"
#include "studio/panel_colours.h"
#include "studio/studio_app.h"

namespace coney::studio {

namespace {

// The Level view's near clip (its view window is kLevelHalfHeight, studio_app.h).
constexpr float kLevelNearClip = 0.1F;

} // namespace

void StudioApp::drawLevelView(std::uint32_t steps, float stepSeconds, float alpha) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
    const bool shown = ImGui::Begin(panels::kLevel);
    ImGui::PopStyleVar();
    if (!shown) {
        setFlying(false);
        ImGui::End();
        return;
    }
    const ImVec2 size = ImGui::GetContentRegionAvail();
    if (!m_level || size.x < 2.0F || size.y < 2.0F) {
        ImGui::TextDisabled("  Open a project to see its level here");
        ImGui::End();
        return;
    }
    // 1. The panel's area takes the mouse: right drag flies, a left click selects.
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##levelview", size,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    const FlyInput input = readFlyInput(hovered);
    // The view window's width follows the panel's shape; a frame asked for before (F, a double-click, --frame) starts
    // its glide now that it is known.
    m_levelHalfWidth = kLevelHalfHeight * size.x / size.y;
    // The docked layout settles in the first frames, so the panel's shape is only trusted from the third.
    if (m_framePending && ImGui::GetFrameCount() > 2) {
        m_framePending = false;
        startFraming();
    }
    // 2. The camera's steps: the mouse movement and the wheel go into the first, the keys into every one.
    for (std::uint32_t i = 0; i < steps; ++i) {
        m_camera.commit();
        FlyInput step = input;
        if (i == 0) {
            step.lookX = m_pendingLookX;
            step.lookY = m_pendingLookY;
            step.wheel = m_pendingWheel;
            m_pendingLookX = m_pendingLookY = m_pendingWheel = 0.0F;
        } else {
            step.lookX = step.lookY = step.wheel = 0.0F;
        }
        m_camera.current().step(step, stepSeconds);
    }
    // 3. The level drawn by the host into a texture, then the markers and the selection over it.
    const LevelView view = levelView(blendedPose(alpha), size.x, size.y, kLevelHalfHeight, kLevelNearClip);
    const std::uint64_t texture = m_host.drawLevel(*m_level, view);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 end(origin.x + size.x, origin.y + size.y);
    if (texture != 0) {
        // The texture is an OpenGL frame buffer's, its first row at the bottom.
        draw->AddImage(ImTextureRef(static_cast<ImTextureID>(texture)), origin, end, ImVec2(0.0F, 1.0F),
                       ImVec2(1.0F, 0.0F));
    } else {
        draw->AddRectFilled(origin, end, IM_COL32(40, 46, 56, 255));
    }
    drawLevelMarkers(view, origin.x, origin.y);
    // 4. A click that did not fly selects what is under it (docs/guides/studio/level-view.md#selection).
    if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
        !ImGui::IsMouseDragPastThreshold(ImGuiMouseButton_Left)) {
        const ImVec2 mouse = ImGui::GetMousePos();
        select(pickEntity(*m_level, view, PanelPoint{.x = mouse.x - origin.x, .y = mouse.y - origin.y}));
    }
    // 5. The camera's speed and the hint, in a corner.
    const std::string hint = m_flying ? std::format("Flying at {:.1f} m/s  (Shift faster, Alt slower, wheel speed)",
                                                    m_camera.current().baseSpeed())
                                      : std::string("Right mouse + W A S D Q E to fly; click to select; F frames it");
    draw->AddText(ImVec2(origin.x + 8.0F, end.y - ImGui::GetTextLineHeight() - 6.0F), IM_COL32(255, 255, 255, 200),
                  hint.c_str());
    ImGui::End();
}

void StudioApp::drawLevelMarkers(const LevelView& view, float left, float top) {
    // Held as a pointer, so the checks below read it once: the draw calls cannot change it.
    const LevelDocument* document = m_level ? &*m_level : nullptr;
    if (document == nullptr) {
        return;
    }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const auto at = [&view, left, top](anim::Vec3 point) -> std::optional<ImVec2> {
        const auto p = coney::studio::project(view, point);
        return p ? std::optional<ImVec2>(ImVec2(left + p->x, top + p->y)) : std::nullopt;
    };
    // The point entities as dots with their names, so a spawn or a fighter can be seen and clicked.
    const float radius = 5.0F * m_settings.uiScale;
    for (const Entity& entity : document->entities()) {
        if (entity.kind == EntityKind::Primitive) {
            continue;
        }
        const anim::Vec3 lift = entity.kind == EntityKind::View ? anim::Vec3{} : anim::Vec3{0.0F, 0.0F, 0.9F};
        if (const auto p = at(anim::add(entity.position, lift))) {
            draw->AddCircleFilled(*p, radius, markerColour(entity.kind));
            draw->AddCircle(*p, radius, IM_COL32(0, 0, 0, 200), 0, 1.5F);
            draw->AddText(ImVec2(p->x + radius + 3.0F, p->y - (ImGui::GetTextLineHeight() * 0.5F)),
                          IM_COL32(255, 255, 255, 220), entity.name.c_str());
        }
    }
    // The selection outlined by its box in the accent colour (Unity outlines the selection).
    const std::optional<EntityId> selection = m_selection;
    const Entity* selected = selection ? document->entity(*selection) : nullptr;
    if (selected == nullptr) {
        return;
    }
    const Bounds box = document->bounds(*selected);
    const std::array<anim::Vec3, 8> corners = {
        anim::Vec3{box.min.x, box.min.y, box.min.z}, anim::Vec3{box.max.x, box.min.y, box.min.z},
        anim::Vec3{box.max.x, box.max.y, box.min.z}, anim::Vec3{box.min.x, box.max.y, box.min.z},
        anim::Vec3{box.min.x, box.min.y, box.max.z}, anim::Vec3{box.max.x, box.min.y, box.max.z},
        anim::Vec3{box.max.x, box.max.y, box.max.z}, anim::Vec3{box.min.x, box.max.y, box.max.z}};
    constexpr std::array<std::array<int, 2>, 12> kEdges = {
        {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}}};
    const ImU32 colour = accentColour(m_settings.theme);
    for (const auto& [a, b] : kEdges) {
        const auto p = at(corners.at(static_cast<std::size_t>(a)));
        const auto q = at(corners.at(static_cast<std::size_t>(b)));
        if (p && q) {
            draw->AddLine(*p, *q, colour, 2.0F * m_settings.uiScale);
        }
    }
}

void StudioApp::setFlying(bool flying) {
    if (flying == m_flying) {
        return;
    }
    m_flying = flying;
    if (flying) {
        // The window keeps the cursor and reports the movement itself, so looking and the wheel never leave it; what
        // moved before the press is dropped.
        m_relativeMouse = m_host.setRelativeMouse(true);
        static_cast<void>(m_host.takeRelativeMotion());
    } else if (m_relativeMouse) {
        m_relativeMouse = false;
        static_cast<void>(m_host.setRelativeMouse(false));
    }
}

FlyInput StudioApp::readFlyInput(bool hovered) {
    // Flying starts with the right button pressed over the view and lasts while it is held, wherever the mouse goes.
    const ImGuiIO& io = ImGui::GetIO();
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        setFlying(true);
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        setFlying(false);
    }
    if (!m_flying) {
        return FlyInput{};
    }
    ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    // The host's relative movement when it gives one (physical pixels), else Dear ImGui's cursor movement.
    const MouseMotion motion =
        m_relativeMouse ? m_host.takeRelativeMotion() : MouseMotion{io.MouseDelta.x, io.MouseDelta.y};
    // The turn per pixel (in FlyInput below) counts them in the display's scale-1 pixels
    // (docs/guides/studio/level-view.md#fly-look).
    m_pendingLookX += motion.x;
    m_pendingLookY += motion.y;
    m_pendingWheel += io.MouseWheel;
    const auto axis = [](ImGuiKey plus, ImGuiKey minus) {
        return (ImGui::IsKeyDown(plus) ? 1.0F : 0.0F) - (ImGui::IsKeyDown(minus) ? 1.0F : 0.0F);
    };
    return FlyInput{.flying = true,
                    .forward = axis(ImGuiKey_W, ImGuiKey_S),
                    .strafe = axis(ImGuiKey_D, ImGuiKey_A),
                    .rise = axis(ImGuiKey_E, ImGuiKey_Q),
                    .fast = io.KeyShift,
                    .slow = io.KeyAlt,
                    .lookRadiansPerPixel =
                        FlyCamera::lookRadiansPerPixel(m_settings.uiScale, m_settings.lookSensitivity)};
}

} // namespace coney::studio
