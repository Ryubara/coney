// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <vector>

#include "core/error.h"
#include "core/pads.h"

// No SDL type appears in this header, so code that holds an SdlInput stays platform-neutral and never includes SDL.

namespace coney::platform {

/// The raw PS2 stick byte for an SDL gamepad axis (-32768 to 32767, right or down positive): 0 at full left or up,
/// 255 at full right or down, 128 at rest.
[[nodiscard]] std::uint8_t stickByteFromAxis(std::int16_t axis);

/// The raw PS2 stick bytes (x, y) for an SDL gamepad stick's two axes, stretched from the circle a modern gamepad
/// reports onto the square a DualShock 2 reports. Pushed fully along a diagonal, a modern stick gives about 0.71 on
/// each axis, which the original's per-axis dead zone (docs/research/frontend.md#pad-record) turns into a length of
/// about 0.86: under the 0.95 a run needs (docs/research/characters.md), so a full diagonal walked. A DualShock 2
/// reaches both extremes there (inferred; PCSX2's default analog sensitivity of 1.33 makes up the same difference), so
/// each point is moved out along its direction until a full circle meets the square's edge; a straight push is
/// unchanged. A Coney choice for modern gamepads, not the original's behaviour.
[[nodiscard]] std::array<std::uint8_t, 2> stickBytesFromAxes(std::int16_t x, std::int16_t y);

/// The pressure byte (0-255) for an SDL trigger axis (0 to 32767; negative values count as 0).
[[nodiscard]] std::uint8_t pressureFromTrigger(std::int16_t axis);

/// The lowest trigger pressure that counts as L2 or R2 held: a quarter of the travel, a Coney choice (the PS2's L2
/// and R2 are buttons with pressure, an SDL trigger is an axis).
inline constexpr std::uint8_t kTriggerHeldPressure = 64;

/// Pad input from SDL3: gamepads through SDL's gamepad API, laid out as a PS2 pad, and the keyboard on port 1.
///
/// The first gamepad found plays on port 1, the second on port 2, in the order they were connected; a gamepad
/// pulled out frees its port for the next. Port 1 is always connected, since the keyboard plays on it too; port 2
/// only while it has a gamepad. The mapping (docs/guides/building.md#controls) is Coney's own: SDL names its face
/// buttons by position, so south is cross, east circle, west square and north triangle on any gamepad.
///
/// It reads device state, so it needs the window's event pump (Window::pumpEvents) to have run before each sample();
/// the stack's loop does that. It also watches the button and key events as they arrive: a button pressed since the
/// last sample counts as held in the next one even if it was let go already, so a tap shorter than a step (or made
/// while a fast display ran frames with no step) is never lost, and it is seen in exactly one step, as the original's
/// pad read once per frame would see it (docs/guides/conventions.md#update-and-render).
///
/// Research: docs/research/frontend.md#pad-record
class SdlInput final : public InputSource {
  public:
    /// Starts SDL's gamepad support. Fails with ErrorCode::PlatformFailure, with SDL's reason, when SDL refuses.
    /// Needs SDL's video started (the RenderEngine's window) for the keyboard, and must be destroyed before it.
    [[nodiscard]] static std::expected<std::unique_ptr<SdlInput>, Error> start();

    ~SdlInput() override;
    SdlInput(const SdlInput&) = delete;
    SdlInput& operator=(const SdlInput&) = delete;
    SdlInput(SdlInput&&) = delete;
    SdlInput& operator=(SdlInput&&) = delete;

    /// The current state of the keyboard and the gamepads, plus every button pressed since the last sample;
    /// `frame` is not needed, the devices are live.
    [[nodiscard]] PortSamples sample(std::uint64_t frame) override;

    /// Records a press of `bit` on the keyboard (`gamepad` 0) or on the gamepad with SDL id `gamepad`, for the next
    /// sample(). The SDL event watch calls it; safe from any thread.
    void notePress(std::uint32_t gamepad, std::uint16_t bit);

    /// Whether the keyboard plays on port 1 (it does by default). The developer overlay turns it off while it has the
    /// keyboard, so typing into it does not also move the player.
    void setKeyboardEnabled(bool enabled) { m_keyboardEnabled = enabled; }

  private:
    SdlInput() = default;

    // Opens gamepads that appeared and closes those that went away, keeping the others in their order.
    void refreshGamepads();

    // A button press seen by the event watch and not sampled yet: the keyboard (gamepad 0) or an SDL gamepad id.
    struct Press {
        std::uint32_t gamepad;
        std::uint16_t bit;
    };
    std::mutex m_pressMutex;      // SDL may call the event watch from another thread
    std::vector<Press> m_presses; // guarded by m_pressMutex
    bool m_watching = false;      // the event watch is installed

    // One open gamepad: SDL's id and its SDL_Gamepad*, kept opaque here.
    struct OpenGamepad {
        std::uint32_t id;
        void* handle;
    };
    std::vector<OpenGamepad> m_gamepads; // in connection order: the first is port 1's
    bool m_keyboardEnabled = true;
};

} // namespace coney::platform
