#pragma once

// Which keys and gamepad inputs drive which controls of a player's controller. Bindings are data: they are read
// from and written to a small text file, so an options screen can change them.
//
//   # comment
//   [keyboard]
//   cross = Space, Return        one or more SDL key names, comma separated; nothing after '=' unbinds
//   left_stick_up = W
//   [gamepad]
//   cross = south                a gamepad button, or an axis: a trigger or stick axis drives a button past half way
//   left_stick_x = left_x        a stick axis takes a gamepad axis
//
// A file only needs the controls it changes; the rest keep their defaults.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "platform/device_events.hpp"
#include "result.hpp"

namespace coney::input {

// The controls of a player's controller: the 16 buttons in button-word bit order, each stick pushed to one side
// (for keys and buttons), and the four stick axes (for gamepad axes).
enum class Control : std::uint8_t {
    L2,
    R2,
    L1,
    R1,
    Triangle,
    Circle,
    Cross,
    Square,
    Select,
    L3,
    R3,
    Start,
    DpadUp,
    DpadRight,
    DpadDown,
    DpadLeft,
    LeftStickUp,
    LeftStickDown,
    LeftStickLeft,
    LeftStickRight,
    RightStickUp,
    RightStickDown,
    RightStickLeft,
    RightStickRight,
    LeftStickX,
    LeftStickY,
    RightStickX,
    RightStickY,
    Count,
};
inline constexpr std::size_t kControlCount = static_cast<std::size_t>(Control::Count);

[[nodiscard]] constexpr bool is_button(Control control) noexcept { return control <= Control::DpadLeft; }
[[nodiscard]] constexpr bool is_stick_axis(Control control) noexcept {
    return control >= Control::LeftStickX && control < Control::Count;
}

[[nodiscard]] std::string_view control_name(Control control) noexcept;
[[nodiscard]] std::optional<Control> parse_control(std::string_view name) noexcept;
[[nodiscard]] std::string_view gamepad_button_name(platform::GamepadButton button) noexcept;
[[nodiscard]] std::string_view gamepad_axis_name(platform::GamepadAxis axis) noexcept;

using GamepadSource = std::variant<platform::GamepadButton, platform::GamepadAxis>;

struct Bindings {
    // Per control, the keys (by SDL key name) and the gamepad inputs that drive it.
    std::array<std::vector<std::string>, kControlCount> keyboard;
    std::array<std::vector<GamepadSource>, kControlCount> gamepad;

    [[nodiscard]] const std::vector<std::string>& keys(Control control) const {
        return keyboard[static_cast<std::size_t>(control)];
    }
    [[nodiscard]] const std::vector<GamepadSource>& gamepad_inputs(Control control) const {
        return gamepad[static_cast<std::size_t>(control)];
    }
    bool operator==(const Bindings&) const = default;
};

// The defaults: engine/README.md lists them.
[[nodiscard]] Bindings default_bindings();

// Reads bindings text over a copy of `base`. The error names the line.
[[nodiscard]] Result<Bindings, std::string> parse_bindings(std::string_view text, const Bindings& base);
// Every control of both sections, in the format parse_bindings reads.
[[nodiscard]] std::string format_bindings(const Bindings& bindings);

// A bindings file over the defaults; the error names the file.
[[nodiscard]] Result<Bindings, std::string> load_bindings_file(const std::string& path);
[[nodiscard]] Result<bool, std::string> save_bindings_file(const std::string& path, const Bindings& bindings);

} // namespace coney::input
