// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "core/error.h"
#include "core/interpolation.h"
#include "gamemodes/game_mode.h"
#include "platform/render_engine.h"
#include "platform/sandbox_renderer.h"
#include "platform/world_renderer.h"
#include "sandbox/sandbox_world.h"
#include "world/debug_camera.h"

namespace coney::platform {

/// The sandbox seen through a free camera, behind `coney --sandbox [NAME]` (docs/guides/sandbox.md): no disc and no
/// game data needed. The camera is the world viewer's debug camera (world::DebugCamera), driven by pad 1 at realistic
/// analog deflections: the left stick flies along the view, the right stick looks, L1 and R1 sink and rise, Cross
/// holds five times the speed. Triangle jumps to the layout's next viewpoint, Square to the one before.
///
/// Every step (update()): the camera's move from pad 1, in game time, or a jump to a viewpoint. Every real frame
/// (render()): the sandbox drawn through the camera blended between its last two steps by the frame's alpha, as the
/// world viewer's camera is (docs/guides/conventions.md#update-and-render).
///
/// Coney's own tool; no counterpart in the original game.
class SandboxViewerMode final : public GameMode {
  public:
    /// The mode's id, outside the original's range.
    static constexpr std::uint32_t kId = 0x107;
    /// How fast the camera flies, metres a second without Cross: about a jog, so a course is easy to follow.
    static constexpr float kFlySpeed = 6.0F;

    /// The viewer of `world`, starting at its first viewpoint. Builds the renderer (SandboxRenderer::create()); fails
    /// as that does. `print` receives one line per viewpoint change. `engine` must outlive the mode.
    [[nodiscard]] static std::expected<std::unique_ptr<SandboxViewerMode>, Error>
    create(RenderEngine& engine, sandbox::SandboxWorld world, std::function<void(std::string_view)> print);

    [[nodiscard]] std::uint32_t id() const override { return kId; }
    /// One step: the camera's move, or a jump to the next or previous viewpoint. Draws nothing.
    ModeResult update(GameModeStack& stack, const FrameTime& frame) override;
    /// Draws the sandbox through the camera blended between the last two steps, and presents.
    void render(const RenderTime& time) override;
    /// One line of counts: frames, the layout's geometry, the viewpoint and where the camera is (game axes).
    [[nodiscard]] std::string summary() const;

    [[nodiscard]] const sandbox::SandboxWorld& world() const { return m_world; }
    [[nodiscard]] const world::DebugCamera& camera() const { return m_camera.current(); }
    /// Moves the camera to viewpoint `index` of the layout (wrapping round).
    void goToView(std::size_t index);

  private:
    SandboxViewerMode(RenderEngine& engine, sandbox::SandboxWorld world, std::unique_ptr<SandboxRenderer> renderer,
                      std::function<void(std::string_view)> print);

    RenderEngine& m_engine;
    sandbox::SandboxWorld m_world;
    std::unique_ptr<SandboxRenderer> m_renderer;
    std::function<void(std::string_view)> m_print;
    Interpolated<world::DebugCamera> m_camera; // at the last two steps
    std::size_t m_view = 0;
    std::uint64_t m_frames = 0;
};

/// A viewpoint of a layout (game axes, yaw 0 along +y and counter-clockwise, pitch up) as the debug camera holds it:
/// in RenderWare's axes, (x, y, z) -> (x, z, -y), with its yaw turned to match.
[[nodiscard]] world::DebugCamera debugCameraAt(const sandbox::Viewpoint& view, float speed);

/// The free camera's view in RenderWare's axes for a frame of `size`: the player camera's lens window, the near clip
/// and, as the far clip, the layout's fog end.
[[nodiscard]] WorldView sandboxView(const world::CameraPose& pose, graphics::Extent size,
                                    const sandbox::Lighting& lighting);

} // namespace coney::platform
