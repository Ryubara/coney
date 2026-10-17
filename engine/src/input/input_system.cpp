#include "input/input_system.hpp"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>

namespace coney::input {

namespace {

using platform::GamepadAxis;
using platform::GamepadButton;

constexpr std::uint8_t kStickLow = 0;
constexpr std::uint8_t kStickHigh = 255;

// A device axis, -32768 to 32767, as the page's stick byte, 0 to 255 with the centre at 128.
std::uint8_t to_stick_byte(std::int16_t value) { return static_cast<std::uint8_t>((value + 32768) >> 8); }

// A stick axis driven by a pair of opposite pushes: full length either way, centred when both or neither are down.
std::uint8_t pushed_byte(bool low, bool high, std::uint8_t otherwise) {
    if (low == high) {
        return low ? game::kStickCentre : otherwise;
    }
    return low ? kStickLow : kStickHigh;
}

template <typename Down> game::PadSticks stick_pushes(Down down, game::PadSticks sticks) {
    sticks.left.x = pushed_byte(down(Control::LeftStickLeft), down(Control::LeftStickRight), sticks.left.x);
    sticks.left.y = pushed_byte(down(Control::LeftStickUp), down(Control::LeftStickDown), sticks.left.y);
    sticks.right.x = pushed_byte(down(Control::RightStickLeft), down(Control::RightStickRight), sticks.right.x);
    sticks.right.y = pushed_byte(down(Control::RightStickUp), down(Control::RightStickDown), sticks.right.y);
    return sticks;
}

template <typename Down> game::ButtonWord button_word(Down down) {
    game::ButtonWord word = 0;
    for (int bit = 0; bit < game::kButtonCount; ++bit) {
        if (down(static_cast<Control>(bit))) {
            word = static_cast<game::ButtonWord>(word | (1u << bit));
        }
    }
    return word;
}

} // namespace

InputSystem::InputSystem(Bindings bindings) : bindings_(std::move(bindings)) {}

InputSystem::Player& InputSystem::at(int player) {
    if (player < 1 || player > game::kPlayerCount) {
        throw std::out_of_range("players are 1 and 2");
    }
    return players_[static_cast<std::size_t>(player - 1)];
}

const InputSystem::Player& InputSystem::at(int player) const {
    return const_cast<InputSystem*>(this)->at(player);
}

void InputSystem::switch_command(int player, int command, bool on) {
    if (command < 0 || command > game::kMostCommandId) {
        return;
    }
    at(player).pad.switched_off.set(static_cast<std::size_t>(command), !on);
}

const PlayerInput& InputSystem::player(int player) const { return at(player).input; }

std::optional<platform::GamepadId> InputSystem::gamepad_of(int player) const { return at(player).gamepad; }

bool InputSystem::keyboard_drives(int player) const {
    const Player& p = at(player);
    return player == 1 && !p.gamepad && !p.lost;
}

bool InputSystem::waiting_for_controller(int player) const { return at(player).lost; }

std::vector<InputEvent> InputSystem::take_events() { return std::exchange(events_, {}); }

InputSystem::Gamepad* InputSystem::find_gamepad(platform::GamepadId id) {
    const auto found = std::ranges::find(gamepads_, id, &Gamepad::id);
    return found == gamepads_.end() ? nullptr : &*found;
}

InputSystem::Player* InputSystem::player_of(platform::GamepadId id) {
    const auto found = std::ranges::find(players_, std::optional{id}, &Player::gamepad);
    return found == players_.end() ? nullptr : &*found;
}

bool InputSystem::control_down(const Keyboard& keyboard, Control control, bool latched) const {
    return std::ranges::any_of(bindings_.keys(control), [&](const std::string& key) {
        return keyboard.down.contains(key) || (latched && keyboard.pressed_since_tick.contains(key));
    });
}

bool InputSystem::control_down(const Gamepad& gamepad, Control control, bool latched) const {
    return std::ranges::any_of(bindings_.gamepad_inputs(control), [&](const GamepadSource& source) {
        if (const auto* button = std::get_if<GamepadButton>(&source)) {
            const auto i = static_cast<std::size_t>(*button);
            return gamepad.down[i] || (latched && gamepad.pressed_since_tick[i]);
        }
        const auto i = static_cast<std::size_t>(std::get<GamepadAxis>(source));
        return gamepad.axes[i] >= kAxisButtonThreshold || (latched && gamepad.past_threshold_since_tick[i]);
    });
}

std::uint8_t InputSystem::axis_byte(const Gamepad& gamepad, Control axis) const {
    std::uint8_t byte = game::kStickCentre;
    for (const GamepadSource& source : bindings_.gamepad_inputs(axis)) {
        const auto* bound = std::get_if<GamepadAxis>(&source);
        if (bound == nullptr) {
            continue;
        }
        const std::uint8_t value = to_stick_byte(gamepad.axes[static_cast<std::size_t>(*bound)]);
        if (std::abs(value - game::kStickCentre) > std::abs(byte - game::kStickCentre)) {
            byte = value; // the axis pushed furthest wins
        }
    }
    return byte;
}

void InputSystem::sample(const Keyboard& keyboard, game::ButtonWord& buttons, game::PadSticks& sticks) const {
    const auto down = [&](Control control) { return control_down(keyboard, control, true); };
    buttons = button_word(down);
    sticks = stick_pushes(down, game::PadSticks{});
}

void InputSystem::sample(const Gamepad& gamepad, game::ButtonWord& buttons, game::PadSticks& sticks) const {
    const auto down = [&](Control control) { return control_down(gamepad, control, true); };
    buttons = button_word(down);
    game::PadSticks from_axes;
    from_axes.left = {axis_byte(gamepad, Control::LeftStickX), axis_byte(gamepad, Control::LeftStickY)};
    from_axes.right = {axis_byte(gamepad, Control::RightStickX), axis_byte(gamepad, Control::RightStickY)};
    sticks = stick_pushes(down, from_axes);
}

void InputSystem::handle(const platform::KeyChanged& event) {
    if (event.down) {
        keyboard_.down.insert(event.key);
        keyboard_.pressed_since_tick.insert(event.key);
    } else {
        keyboard_.down.erase(event.key);
    }
    after_keyboard_change();
}

void InputSystem::handle(const platform::GamepadAdded& event) {
    if (find_gamepad(event.id) != nullptr) {
        return;
    }
    gamepads_.push_back(Gamepad{.id = event.id});
    assign(gamepads_.back());
}

void InputSystem::handle(const platform::GamepadRemoved& event) {
    const auto found = std::ranges::find(gamepads_, event.id, &Gamepad::id);
    if (found == gamepads_.end()) {
        return;
    }
    gamepads_.erase(found);
    Player* player = player_of(event.id);
    if (player == nullptr) {
        return; // a gamepad with no player: nothing happens
    }
    player->gamepad.reset();
    player->lost = true;
    player->pad.history = {};
    player->input = {};
    events_.push_back(ControllerLost{static_cast<int>(player - players_.data()) + 1});
}

void InputSystem::handle(const platform::GamepadButtonChanged& event) {
    Gamepad* gamepad = find_gamepad(event.id);
    if (gamepad == nullptr) {
        return;
    }
    const auto i = static_cast<std::size_t>(event.button);
    gamepad->down[i] = event.down;
    gamepad->pressed_since_tick[i] = gamepad->pressed_since_tick[i] || event.down;
    after_gamepad_change(*gamepad);
}

void InputSystem::handle(const platform::GamepadAxisMoved& event) {
    Gamepad* gamepad = find_gamepad(event.id);
    if (gamepad == nullptr) {
        return;
    }
    const auto i = static_cast<std::size_t>(event.axis);
    gamepad->axes[i] = event.value;
    gamepad->past_threshold_since_tick[i] = gamepad->past_threshold_since_tick[i] ||
                                            event.value >= kAxisButtonThreshold;
    after_gamepad_change(*gamepad);
}

void InputSystem::after_gamepad_change(Gamepad& gamepad) {
    const bool start = control_down(gamepad, Control::Start, false);
    const bool was = std::exchange(gamepad.start_down, start);
    if (start == was || player_of(gamepad.id) != nullptr) {
        return;
    }
    if (start) {
        gamepad.start_armed = true;
        return;
    }
    if (!std::exchange(gamepad.start_armed, false)) {
        return;
    }
    const auto waiting = std::ranges::find(players_, true, &Player::lost);
    if (waiting != players_.end()) {
        give_back(*waiting, gamepad.id);
        gamepad.pressed_since_tick = {};
        gamepad.past_threshold_since_tick = {};
    } else {
        assign(gamepad);
    }
}

void InputSystem::after_keyboard_change() {
    const bool start = control_down(keyboard_, Control::Start, false);
    const bool was = std::exchange(keyboard_.start_down, start);
    Player& first = players_[0];
    if (start == was || !first.lost) {
        return;
    }
    if (start) {
        keyboard_.start_armed = true;
        return;
    }
    if (std::exchange(keyboard_.start_armed, false)) {
        give_back(first, std::nullopt);
        keyboard_.pressed_since_tick.clear();
    }
}

void InputSystem::give_back(Player& player, std::optional<platform::GamepadId> gamepad) {
    player.lost = false;
    player.gamepad = gamepad;
    player.pad.history = {};
    player.input = {};
    events_.push_back(ControllerRestored{static_cast<int>(&player - players_.data()) + 1, gamepad});
}

void InputSystem::assign(Gamepad& gamepad) {
    if (std::ranges::any_of(players_, &Player::lost)) {
        return; // a waiting player gets the next controller whose START is pressed
    }
    const auto free = std::ranges::find(players_, std::optional<platform::GamepadId>{}, &Player::gamepad);
    if (free == players_.end()) {
        return;
    }
    free->gamepad = gamepad.id;
    free->pad.history = {};
    free->input = {};
    gamepad.start_armed = false;
    events_.push_back(ControllerAssigned{static_cast<int>(free - players_.data()) + 1, gamepad.id});
}

void InputSystem::tick() {
    for (std::size_t i = 0; i < players_.size(); ++i) {
        Player& player = players_[i];
        if (player.lost) {
            player.input = {};
            continue;
        }
        game::ButtonWord buttons = 0;
        game::PadSticks sticks;
        if (player.gamepad) {
            if (const Gamepad* gamepad = find_gamepad(*player.gamepad)) {
                sample(*gamepad, buttons, sticks);
            }
        } else if (i == 0) {
            sample(keyboard_, buttons, sticks);
        }
        player.input.stick_bytes = sticks;
        player.input.sticks = game::convert_sticks(sticks);
        player.input.command = game::update_pad_commands(player.pad, buttons, commands_);
        player.input.buttons = player.pad.history.words[0];
    }
    keyboard_.pressed_since_tick.clear();
    for (Gamepad& gamepad : gamepads_) {
        gamepad.pressed_since_tick = {};
        gamepad.past_threshold_since_tick = {};
    }
}

} // namespace coney::input
