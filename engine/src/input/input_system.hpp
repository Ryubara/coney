#pragma once

// Turns the platform layer's keyboard and gamepad events into one controller per player, so game code never sees a
// device. Players 1 and 2 have one controller each: a connected gamepad takes the first free player, and the
// keyboard drives player 1 while no gamepad does. Each game tick samples every player's devices into the button
// word and stick bytes, and the game's pad code (src/game/pad.hpp) turns them into stick values and a command.
//
// Hot-plug follows the page's Pulled controller rules: a gamepad pulled from a player raises ControllerLost and
// leaves the player waiting; START pressed and released on a gamepad with no player (or on the keyboard, for
// player 1) gives the waiting player back. A gamepad with no player that is pulled raises nothing.

#include <array>
#include <optional>
#include <string>
#include <unordered_set>
#include <variant>
#include <vector>

#include "game/pad.hpp"
#include "input/bindings.hpp"
#include "platform/device_events.hpp"

namespace coney::input {

// Players are numbered 1 and 2 in events, as on the page's messages.
struct ControllerAssigned {
    int player = 0;
    platform::GamepadId gamepad = 0;
};
struct ControllerLost {
    int player = 0;
};
// `gamepad` is empty when the keyboard took the player back.
struct ControllerRestored {
    int player = 0;
    std::optional<platform::GamepadId> gamepad;
};
using InputEvent = std::variant<ControllerAssigned, ControllerLost, ControllerRestored>;

// What a player's controller gave on the last tick.
struct PlayerInput {
    game::ButtonWord buttons = 0; // after the d-pad rule
    game::PadSticks stick_bytes;
    game::PlayerSticks sticks;
    std::optional<int> command;
};

// A trigger or stick axis counts as a button down from half its travel.
inline constexpr int kAxisButtonThreshold = 16384;

class InputSystem {
  public:
    explicit InputSystem(Bindings bindings = default_bindings());

    void set_bindings(Bindings bindings) { bindings_ = std::move(bindings); }
    [[nodiscard]] const Bindings& bindings() const noexcept { return bindings_; }

    // The command bindings every player's controller is matched against.
    void set_command_bindings(std::vector<game::CommandBinding> bindings) { commands_ = std::move(bindings); }
    // Switches a command off or on for one player (1 or 2).
    void switch_command(int player, int command, bool on);

    // Takes a keyboard or gamepad event; other events are ignored.
    void handle(const platform::KeyChanged& event);
    void handle(const platform::GamepadAdded& event);
    void handle(const platform::GamepadRemoved& event);
    void handle(const platform::GamepadButtonChanged& event);
    void handle(const platform::GamepadAxisMoved& event);
    template <typename... Events> void handle(const std::variant<Events...>& event) {
        std::visit(
            [this](const auto& e) {
                if constexpr (requires(InputSystem& system) { system.handle(e); }) {
                    this->handle(e);
                }
            },
            event);
    }

    // One game tick: samples each player's devices and updates the player's controller. A button that went down
    // and up again since the last tick counts as down for this one.
    void tick();

    // Player 1 or 2.
    [[nodiscard]] const PlayerInput& player(int player) const;
    [[nodiscard]] std::optional<platform::GamepadId> gamepad_of(int player) const;
    [[nodiscard]] bool keyboard_drives(int player) const;
    [[nodiscard]] bool waiting_for_controller(int player) const;

    // The events raised since the last call, oldest first.
    [[nodiscard]] std::vector<InputEvent> take_events();

  private:
    struct Gamepad {
        platform::GamepadId id = 0;
        std::array<bool, static_cast<std::size_t>(platform::GamepadButton::Count)> down{};
        std::array<bool, static_cast<std::size_t>(platform::GamepadButton::Count)> pressed_since_tick{};
        std::array<std::int16_t, static_cast<std::size_t>(platform::GamepadAxis::Count)> axes{};
        std::array<bool, static_cast<std::size_t>(platform::GamepadAxis::Count)> past_threshold_since_tick{};
        bool start_down = false;
        bool start_armed = false; // START went down while the gamepad had no player
    };
    struct Keyboard {
        std::unordered_set<std::string> down;
        std::unordered_set<std::string> pressed_since_tick;
        bool start_down = false;
        bool start_armed = false;
    };
    struct Player {
        std::optional<platform::GamepadId> gamepad;
        bool lost = false;
        game::PlayerPad pad;
        PlayerInput input;
    };

    [[nodiscard]] Gamepad* find_gamepad(platform::GamepadId id);
    [[nodiscard]] Player* player_of(platform::GamepadId id);
    [[nodiscard]] bool control_down(const Keyboard& keyboard, Control control, bool latched) const;
    [[nodiscard]] bool control_down(const Gamepad& gamepad, Control control, bool latched) const;
    [[nodiscard]] std::uint8_t axis_byte(const Gamepad& gamepad, Control axis) const;
    void sample(const Keyboard& keyboard, game::ButtonWord& buttons, game::PadSticks& sticks) const;
    void sample(const Gamepad& gamepad, game::ButtonWord& buttons, game::PadSticks& sticks) const;
    void after_gamepad_change(Gamepad& gamepad);
    void after_keyboard_change();
    void give_back(Player& player, std::optional<platform::GamepadId> gamepad);
    void assign(Gamepad& gamepad);
    [[nodiscard]] Player& at(int player);
    [[nodiscard]] const Player& at(int player) const;

    Bindings bindings_;
    std::vector<game::CommandBinding> commands_;
    Keyboard keyboard_;
    std::vector<Gamepad> gamepads_; // in the order they were added
    std::array<Player, game::kPlayerCount> players_;
    std::vector<InputEvent> events_;
};

} // namespace coney::input
