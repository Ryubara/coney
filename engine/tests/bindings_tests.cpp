#include "input/bindings.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

using namespace coney::input;
using coney::platform::GamepadAxis;
using coney::platform::GamepadButton;

TEST_CASE("every button and stick push has a default key, and every control a gamepad input") {
    const Bindings b = default_bindings();
    for (std::size_t i = 0; i < kControlCount; ++i) {
        const auto control = static_cast<Control>(i);
        INFO(control_name(control));
        CHECK(b.keys(control).empty() == is_stick_axis(control));
        if (is_button(control) || is_stick_axis(control)) {
            CHECK_FALSE(b.gamepad_inputs(control).empty());
        }
    }
    CHECK(b.gamepad_inputs(Control::Cross) == std::vector<GamepadSource>{GamepadButton::South});
    CHECK(b.gamepad_inputs(Control::L2) == std::vector<GamepadSource>{GamepadAxis::LeftTrigger});
}

TEST_CASE("control names round-trip") {
    for (std::size_t i = 0; i < kControlCount; ++i) {
        const auto control = static_cast<Control>(i);
        CHECK(parse_control(control_name(control)) == control);
    }
    CHECK_FALSE(parse_control("jump"));
}

TEST_CASE("the bindings text round-trips") {
    const Bindings defaults = default_bindings();
    const auto parsed = parse_bindings(format_bindings(defaults), Bindings{});
    REQUIRE(parsed);
    CHECK(*parsed == defaults);
}

TEST_CASE("a bindings file changes only the controls it names") {
    const auto parsed = parse_bindings("# mine\n"
                                       "[keyboard]\n"
                                       "cross = K, Return   # two keys\n"
                                       "square =\n"
                                       "[gamepad]\n"
                                       "r2 = right_shoulder, right_trigger\n"
                                       "left_stick_x = right_x\n",
                                       default_bindings());
    REQUIRE(parsed);
    CHECK(parsed->keys(Control::Cross) == std::vector<std::string>{"K", "Return"});
    CHECK(parsed->keys(Control::Square).empty());
    CHECK(parsed->keys(Control::Triangle) == default_bindings().keys(Control::Triangle));
    CHECK(parsed->gamepad_inputs(Control::R2) ==
          std::vector<GamepadSource>{GamepadButton::RightShoulder, GamepadAxis::RightTrigger});
    CHECK(parsed->gamepad_inputs(Control::LeftStickX) == std::vector<GamepadSource>{GamepadAxis::RightX});
}

TEST_CASE("bad bindings text names the line") {
    const Bindings base = default_bindings();
    CHECK(parse_bindings("cross = K\n", base).error() == "line 1: a binding before [keyboard] or [gamepad]");
    CHECK(parse_bindings("[mouse]\n", base).error() == "line 1: unknown section [mouse]");
    CHECK(parse_bindings("[keyboard]\n\njump = K\n", base).error() == "line 3: unknown control jump");
    CHECK(parse_bindings("[keyboard]\ncross K\n", base).error() == "line 2: expected 'control = inputs'");
    CHECK(parse_bindings("[keyboard]\ncross = K,,L\n", base).error() == "line 2: an empty key name");
    CHECK(parse_bindings("[keyboard]\nleft_stick_x = A\n", base).error() ==
          "line 2: left_stick_x takes a gamepad axis, not keys");
    CHECK(parse_bindings("[gamepad]\ncross = a\n", base).error() == "line 2: unknown gamepad input a");
    CHECK(parse_bindings("[gamepad]\nleft_stick_y = south\n", base).error() ==
          "line 2: left_stick_y takes a gamepad axis, not a button");
}

TEST_CASE("bindings files are written and read back") {
    const auto dir = std::filesystem::temp_directory_path() / "coney_bindings_tests";
    std::filesystem::create_directories(dir);
    const std::string path = (dir / "bindings.ini").string();

    Bindings mine = default_bindings();
    mine.keyboard[static_cast<std::size_t>(Control::Start)] = {"P"};
    REQUIRE(save_bindings_file(path, mine));
    const auto loaded = load_bindings_file(path);
    REQUIRE(loaded);
    CHECK(*loaded == mine);

    std::ofstream(path) << "[gamepad]\nstart = nothing\n";
    const auto bad = load_bindings_file(path);
    REQUIRE_FALSE(bad);
    CHECK(bad.error() == path + ": line 2: unknown gamepad input nothing");

    CHECK_FALSE(load_bindings_file((dir / "missing.ini").string()));
    std::filesystem::remove_all(dir);
}
