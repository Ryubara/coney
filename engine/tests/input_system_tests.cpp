// The input layer over synthetic platform events: devices to players, bindings, hot-plug.

#include "input/input_system.hpp"
#include "platform/platform.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace coney;
using namespace coney::input;
using platform::GamepadAxis;
using platform::GamepadButton;
namespace button = game::button;

namespace {

platform::KeyChanged key(std::string name, bool down) { return {std::move(name), down}; }
platform::GamepadButtonChanged pad_button(platform::GamepadId id, GamepadButton b, bool down) {
    return {id, b, down};
}
platform::GamepadAxisMoved pad_axis(platform::GamepadId id, GamepadAxis axis, std::int16_t value) {
    return {id, axis, value};
}

void press_start(InputSystem& input, platform::GamepadId id) {
    input.handle(pad_button(id, GamepadButton::Start, true));
    input.handle(pad_button(id, GamepadButton::Start, false));
}

template <typename T> bool only_event(InputSystem& input, const T& wanted) {
    const auto events = input.take_events();
    if (events.size() != 1 || !std::holds_alternative<T>(events[0])) {
        return false;
    }
    const T& got = std::get<T>(events[0]);
    if constexpr (requires { got.gamepad; }) {
        if (got.gamepad != wanted.gamepad) {
            return false;
        }
    }
    return got.player == wanted.player;
}

} // namespace

TEST_CASE("with no gamepad the keyboard drives player 1") {
    InputSystem input;
    CHECK(input.keyboard_drives(1));
    CHECK_FALSE(input.keyboard_drives(2));

    input.handle(key("Space", true));
    input.handle(key("W", true));
    input.tick();
    CHECK(input.player(1).buttons == button::kCross);
    CHECK(input.player(2).buttons == 0);

    // Keyboard directions give full-length values; a diagonal is capped at 1.
    CHECK(input.player(1).sticks.left.y == 1.0f);
    CHECK(input.player(1).sticks.left.length == 1.0f);
    input.handle(key("D", true));
    input.tick();
    CHECK(input.player(1).sticks.left.x == 1.0f);
    CHECK(input.player(1).sticks.left.y == 1.0f);
    CHECK(input.player(1).sticks.left.length == 1.0f);
    // Opposite keys cancel out.
    input.handle(key("A", true));
    input.tick();
    CHECK(input.player(1).sticks.left.x == 0.0f);

    input.handle(key("J", true));
    input.tick();
    CHECK(input.player(1).sticks.right.x == -1.0f);
}

TEST_CASE("a key and a gamepad button bound to one control give the same button") {
    Bindings b = default_bindings();
    b.keyboard[static_cast<std::size_t>(Control::Cross)] = {"K", "Space"};
    b.gamepad[static_cast<std::size_t>(Control::Cross)] = {GamepadButton::South, GamepadButton::Misc1};
    InputSystem input(b);

    input.handle(key("K", true));
    input.tick();
    CHECK(input.player(1).buttons == button::kCross);
    input.handle(key("K", false));
    input.handle(key("Space", true));
    input.tick();
    CHECK(input.player(1).buttons == button::kCross);
    input.handle(key("Space", false));

    input.handle(platform::GamepadAdded{7, "pad"});
    input.handle(pad_button(7, GamepadButton::Misc1, true));
    input.tick();
    CHECK(input.player(1).buttons == button::kCross);
    input.handle(pad_button(7, GamepadButton::Misc1, false));
    input.handle(pad_button(7, GamepadButton::South, true));
    input.tick();
    CHECK(input.player(1).buttons == button::kCross);

    // With a gamepad driving player 1 the keyboard no longer does.
    input.handle(pad_button(7, GamepadButton::South, false));
    input.handle(key("K", true));
    input.tick();
    CHECK(input.player(1).buttons == 0);
}

TEST_CASE("gamepad axes become stick bytes, and triggers buttons past half way") {
    InputSystem input;
    input.handle(platform::GamepadAdded{3, "pad"});
    input.handle(pad_axis(3, GamepadAxis::LeftX, -32768));
    input.handle(pad_axis(3, GamepadAxis::LeftY, 32767));
    input.handle(pad_axis(3, GamepadAxis::RightX, 0));
    input.handle(pad_axis(3, GamepadAxis::LeftTrigger, 16383));
    input.handle(pad_axis(3, GamepadAxis::RightTrigger, 16384));
    input.tick();
    const PlayerInput& p = input.player(1);
    CHECK(p.stick_bytes.left.x == 0);
    CHECK(p.stick_bytes.left.y == 255);
    CHECK(p.stick_bytes.right.x == 128);
    CHECK(p.sticks.left.x == -1.0f);
    CHECK(p.sticks.left.y == -1.0f); // pushed down
    CHECK(p.buttons == button::kR2);
}

TEST_CASE("a button down and up between two ticks still counts once") {
    InputSystem input;
    input.set_command_bindings({{15, game::TriggerKind::Pressed, button::kSquare, 0}});
    input.handle(key("F", true));
    input.handle(key("F", false));
    input.tick();
    CHECK(input.player(1).buttons == button::kSquare);
    CHECK(input.player(1).command == 15);
    input.tick();
    CHECK(input.player(1).buttons == 0);
    CHECK(input.player(1).command == std::nullopt);
}

TEST_CASE("each player's controller gives its own commands") {
    InputSystem input;
    input.set_command_bindings({{15, game::TriggerKind::Pressed, button::kSquare, 0},
                                {21, game::TriggerKind::Held, button::kSquare, 0}});
    input.handle(platform::GamepadAdded{1, "one"});
    input.handle(platform::GamepadAdded{2, "two"});
    input.handle(pad_button(2, GamepadButton::West, true));
    input.tick();
    CHECK(input.player(1).command == std::nullopt);
    CHECK(input.player(2).command == 15);
    input.switch_command(2, 21, false);
    input.tick();
    CHECK(input.player(2).command == std::nullopt);
    input.switch_command(2, 21, true);
    input.tick();
    CHECK(input.player(2).command == 21);
}

TEST_CASE("gamepads take the first free player") {
    InputSystem input;
    input.handle(platform::GamepadAdded{10, "a"});
    CHECK(only_event(input, ControllerAssigned{1, 10}));
    input.handle(platform::GamepadAdded{11, "b"});
    CHECK(only_event(input, ControllerAssigned{2, 11}));
    input.handle(platform::GamepadAdded{12, "c"});
    CHECK(input.take_events().empty());
    CHECK(input.gamepad_of(1) == 10u);
    CHECK(input.gamepad_of(2) == 11u);
    CHECK_FALSE(input.keyboard_drives(1));
}

TEST_CASE("a pad added, pulled and given back") {
    InputSystem input;
    input.set_command_bindings({{3, game::TriggerKind::Pressed, button::kR1, 0}});
    input.handle(platform::GamepadAdded{20, "pad"});
    CHECK(only_event(input, ControllerAssigned{1, 20}));
    input.handle(pad_button(20, GamepadButton::RightShoulder, true));
    input.tick();
    CHECK(input.player(1).command == 3);

    input.handle(platform::GamepadRemoved{20});
    CHECK(only_event(input, ControllerLost{1}));
    CHECK(input.waiting_for_controller(1));
    CHECK_FALSE(input.keyboard_drives(1));
    input.handle(key("Space", true));
    input.tick();
    CHECK(input.player(1).buttons == 0); // nothing drives a waiting player
    input.handle(key("Space", false));

    // Put back, it comes with a new id and waits for START; it does not take player 2 meanwhile.
    input.handle(platform::GamepadAdded{21, "pad"});
    CHECK(input.take_events().empty());
    CHECK(input.gamepad_of(2) == std::nullopt);
    input.handle(pad_button(21, GamepadButton::Start, true));
    CHECK(input.take_events().empty()); // pressed and released
    input.handle(pad_button(21, GamepadButton::Start, false));
    CHECK(only_event(input, ControllerRestored{1, 21}));
    CHECK_FALSE(input.waiting_for_controller(1));
    CHECK(input.gamepad_of(1) == 21u);

    // The START that gave it back does not reach the player.
    input.tick();
    CHECK(input.player(1).buttons == 0);
    input.handle(pad_button(21, GamepadButton::RightShoulder, true));
    input.tick();
    CHECK(input.player(1).command == 3);
}

TEST_CASE("a held button when the pad is pulled does not count as released") {
    InputSystem input;
    input.set_command_bindings({{2, game::TriggerKind::Released, button::kR2, 0}});
    input.handle(platform::GamepadAdded{1, "pad"});
    input.handle(pad_axis(1, GamepadAxis::RightTrigger, 32767));
    input.tick();
    input.handle(platform::GamepadRemoved{1});
    input.tick();
    CHECK(input.player(1).command == std::nullopt);
    input.handle(platform::GamepadAdded{2, "pad"});
    press_start(input, 2);
    input.tick();
    CHECK(input.player(1).command == std::nullopt);
}

TEST_CASE("player 2's pad given back while player 1 keeps its own") {
    InputSystem input;
    input.handle(platform::GamepadAdded{1, "one"});
    input.handle(platform::GamepadAdded{2, "two"});
    (void)input.take_events();
    input.handle(platform::GamepadRemoved{2});
    CHECK(only_event(input, ControllerLost{2}));
    press_start(input, 1); // START on player 1's own pad does not move it
    CHECK(input.take_events().empty());
    input.handle(platform::GamepadAdded{3, "two again"});
    press_start(input, 3);
    CHECK(only_event(input, ControllerRestored{2, 3}));
}

TEST_CASE("with every pad pulled, START on the keyboard gives player 1 back") {
    InputSystem input;
    input.handle(platform::GamepadAdded{5, "pad"});
    (void)input.take_events();
    input.handle(platform::GamepadRemoved{5});
    CHECK(only_event(input, ControllerLost{1}));
    input.handle(key("Return", true));
    input.handle(key("Return", false));
    CHECK(only_event(input, ControllerRestored{1, std::nullopt}));
    CHECK(input.keyboard_drives(1));
    input.tick();
    CHECK(input.player(1).buttons == 0); // nor does this START reach the player
}

TEST_CASE("a pad with no player that is pulled raises nothing") {
    InputSystem input;
    input.handle(platform::GamepadAdded{1, "one"});
    input.handle(platform::GamepadAdded{2, "two"});
    input.handle(platform::GamepadAdded{3, "spare"});
    (void)input.take_events();
    input.handle(platform::GamepadRemoved{3});
    CHECK(input.take_events().empty());
    CHECK_FALSE(input.waiting_for_controller(1));
    CHECK_FALSE(input.waiting_for_controller(2));
    input.handle(platform::GamepadRemoved{99}); // never seen
    CHECK(input.take_events().empty());
}

TEST_CASE("platform events go through the variant overload") {
    InputSystem input;
    const platform::Event added = platform::GamepadAdded{4, "pad"};
    const platform::Event close = platform::CloseRequested{};
    input.handle(added);
    input.handle(close);
    CHECK(only_event(input, ControllerAssigned{1, 4}));
}
