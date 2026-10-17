#include "input/bindings.hpp"

#include <format>
#include <fstream>
#include <sstream>

namespace coney::input {

namespace {

using platform::GamepadAxis;
using platform::GamepadButton;

constexpr std::array<std::string_view, kControlCount> kControlNames{
    "l2",
    "r2",
    "l1",
    "r1",
    "triangle",
    "circle",
    "cross",
    "square",
    "select",
    "l3",
    "r3",
    "start",
    "dpad_up",
    "dpad_right",
    "dpad_down",
    "dpad_left",
    "left_stick_up",
    "left_stick_down",
    "left_stick_left",
    "left_stick_right",
    "right_stick_up",
    "right_stick_down",
    "right_stick_left",
    "right_stick_right",
    "left_stick_x",
    "left_stick_y",
    "right_stick_x",
    "right_stick_y",
};

constexpr std::array<std::string_view, static_cast<std::size_t>(GamepadButton::Count)> kButtonNames{
    "south",
    "east",
    "west",
    "north",
    "back",
    "guide",
    "start",
    "left_stick",
    "right_stick",
    "left_shoulder",
    "right_shoulder",
    "dpad_up",
    "dpad_down",
    "dpad_left",
    "dpad_right",
    "misc1",
    "right_paddle1",
    "left_paddle1",
    "right_paddle2",
    "left_paddle2",
    "touchpad",
};

constexpr std::array<std::string_view, static_cast<std::size_t>(GamepadAxis::Count)> kAxisNames{
    "left_x", "left_y", "right_x", "right_y", "left_trigger", "right_trigger",
};

std::string_view trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r");
    return text.substr(first, last - first + 1);
}

std::vector<std::string_view> split_list(std::string_view text) {
    std::vector<std::string_view> items;
    while (!text.empty()) {
        const auto comma = text.find(',');
        items.push_back(trim(text.substr(0, comma)));
        if (comma == std::string_view::npos) {
            break;
        }
        text.remove_prefix(comma + 1);
    }
    return items;
}

std::optional<GamepadSource> parse_gamepad_source(std::string_view name) {
    for (std::size_t i = 0; i < kButtonNames.size(); ++i) {
        if (kButtonNames[i] == name) {
            return GamepadSource{static_cast<GamepadButton>(i)};
        }
    }
    for (std::size_t i = 0; i < kAxisNames.size(); ++i) {
        if (kAxisNames[i] == name) {
            return GamepadSource{static_cast<GamepadAxis>(i)};
        }
    }
    return std::nullopt;
}

std::string_view source_name(const GamepadSource& source) {
    if (const auto* button = std::get_if<GamepadButton>(&source)) {
        return gamepad_button_name(*button);
    }
    return gamepad_axis_name(std::get<GamepadAxis>(source));
}

void bind(Bindings& b, Control control, std::vector<std::string> keys, std::vector<GamepadSource> pad) {
    b.keyboard[static_cast<std::size_t>(control)] = std::move(keys);
    b.gamepad[static_cast<std::size_t>(control)] = std::move(pad);
}

} // namespace

std::string_view control_name(Control control) noexcept {
    const auto i = static_cast<std::size_t>(control);
    return i < kControlNames.size() ? kControlNames[i] : std::string_view{};
}

std::optional<Control> parse_control(std::string_view name) noexcept {
    for (std::size_t i = 0; i < kControlNames.size(); ++i) {
        if (kControlNames[i] == name) {
            return static_cast<Control>(i);
        }
    }
    return std::nullopt;
}

std::string_view gamepad_button_name(GamepadButton button) noexcept {
    const auto i = static_cast<std::size_t>(button);
    return i < kButtonNames.size() ? kButtonNames[i] : std::string_view{};
}

std::string_view gamepad_axis_name(GamepadAxis axis) noexcept {
    const auto i = static_cast<std::size_t>(axis);
    return i < kAxisNames.size() ? kAxisNames[i] : std::string_view{};
}

Bindings default_bindings() {
    Bindings b;
    bind(b, Control::L2, {"Left Shift"}, {GamepadAxis::LeftTrigger});
    bind(b, Control::R2, {"Tab"}, {GamepadAxis::RightTrigger});
    bind(b, Control::L1, {"Q"}, {GamepadButton::LeftShoulder});
    bind(b, Control::R1, {"R"}, {GamepadButton::RightShoulder});
    bind(b, Control::Triangle, {"E"}, {GamepadButton::North});
    bind(b, Control::Circle, {"C"}, {GamepadButton::East});
    bind(b, Control::Cross, {"Space"}, {GamepadButton::South});
    bind(b, Control::Square, {"F"}, {GamepadButton::West});
    bind(b, Control::Select, {"Backspace"}, {GamepadButton::Back});
    bind(b, Control::L3, {"Z"}, {GamepadButton::LeftStick});
    bind(b, Control::R3, {"X"}, {GamepadButton::RightStick});
    bind(b, Control::Start, {"Return", "Escape"}, {GamepadButton::Start});
    bind(b, Control::DpadUp, {"Up"}, {GamepadButton::DpadUp});
    bind(b, Control::DpadRight, {"Right"}, {GamepadButton::DpadRight});
    bind(b, Control::DpadDown, {"Down"}, {GamepadButton::DpadDown});
    bind(b, Control::DpadLeft, {"Left"}, {GamepadButton::DpadLeft});
    bind(b, Control::LeftStickUp, {"W"}, {});
    bind(b, Control::LeftStickDown, {"S"}, {});
    bind(b, Control::LeftStickLeft, {"A"}, {});
    bind(b, Control::LeftStickRight, {"D"}, {});
    bind(b, Control::RightStickUp, {"I"}, {});
    bind(b, Control::RightStickDown, {"K"}, {});
    bind(b, Control::RightStickLeft, {"J"}, {});
    bind(b, Control::RightStickRight, {"L"}, {});
    bind(b, Control::LeftStickX, {}, {GamepadAxis::LeftX});
    bind(b, Control::LeftStickY, {}, {GamepadAxis::LeftY});
    bind(b, Control::RightStickX, {}, {GamepadAxis::RightX});
    bind(b, Control::RightStickY, {}, {GamepadAxis::RightY});
    return b;
}

Result<Bindings, std::string> parse_bindings(std::string_view text, const Bindings& base) {
    enum class Section { None, Keyboard, Gamepad };
    Bindings out = base;
    Section section = Section::None;
    int line_number = 0;

    while (!text.empty()) {
        const auto newline = text.find('\n');
        const std::string_view raw = text.substr(0, newline);
        text.remove_prefix(newline == std::string_view::npos ? text.size() : newline + 1);
        ++line_number;
        const auto fail = [&](std::string_view why) {
            return Failure{std::format("line {}: {}", line_number, why)};
        };

        const std::string_view line = trim(raw.substr(0, raw.find('#')));
        if (line.empty()) {
            continue;
        }
        if (line.front() == '[') {
            if (line == "[keyboard]") {
                section = Section::Keyboard;
            } else if (line == "[gamepad]") {
                section = Section::Gamepad;
            } else {
                return fail(std::format("unknown section {}", line));
            }
            continue;
        }
        const auto equals = line.find('=');
        if (equals == std::string_view::npos) {
            return fail("expected 'control = inputs'");
        }
        if (section == Section::None) {
            return fail("a binding before [keyboard] or [gamepad]");
        }
        const std::string_view name = trim(line.substr(0, equals));
        const auto control = parse_control(name);
        if (!control) {
            return fail(std::format("unknown control {}", name));
        }
        const auto index = static_cast<std::size_t>(*control);
        const std::string_view list = trim(line.substr(equals + 1));
        const auto items = list.empty() ? std::vector<std::string_view>{} : split_list(list);

        if (section == Section::Keyboard) {
            if (is_stick_axis(*control) && !items.empty()) {
                return fail(std::format("{} takes a gamepad axis, not keys", name));
            }
            std::vector<std::string> keys;
            for (const auto item : items) {
                if (item.empty()) {
                    return fail("an empty key name");
                }
                keys.emplace_back(item);
            }
            out.keyboard[index] = std::move(keys);
        } else {
            std::vector<GamepadSource> sources;
            for (const auto item : items) {
                const auto source = parse_gamepad_source(item);
                if (!source) {
                    return fail(std::format("unknown gamepad input {}", item));
                }
                if (is_stick_axis(*control) && !std::holds_alternative<GamepadAxis>(*source)) {
                    return fail(std::format("{} takes a gamepad axis, not a button", name));
                }
                sources.push_back(*source);
            }
            out.gamepad[index] = std::move(sources);
        }
    }
    return out;
}

std::string format_bindings(const Bindings& bindings) {
    std::string out = "# Coney input bindings. Controls left out keep their defaults; nothing after '=' unbinds.\n";
    out += "\n[keyboard]\n";
    for (std::size_t i = 0; i < kControlCount; ++i) {
        const auto control = static_cast<Control>(i);
        if (is_stick_axis(control)) {
            continue;
        }
        out += std::format("{} =", control_name(control));
        const char* separator = " ";
        for (const auto& key : bindings.keyboard[i]) {
            out += separator + key;
            separator = ", ";
        }
        out += '\n';
    }
    out += "\n[gamepad]\n";
    for (std::size_t i = 0; i < kControlCount; ++i) {
        out += std::format("{} =", control_name(static_cast<Control>(i)));
        const char* separator = " ";
        for (const auto& source : bindings.gamepad[i]) {
            out += separator;
            out += source_name(source);
            separator = ", ";
        }
        out += '\n';
    }
    return out;
}

Result<Bindings, std::string> load_bindings_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return Failure{std::format("cannot read the bindings file {}", path)};
    }
    std::ostringstream text;
    text << file.rdbuf();
    auto parsed = parse_bindings(text.str(), default_bindings());
    if (!parsed) {
        return Failure{std::format("{}: {}", path, parsed.error())};
    }
    return parsed;
}

Result<bool, std::string> save_bindings_file(const std::string& path, const Bindings& bindings) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << format_bindings(bindings);
    file.close();
    if (!file) {
        return Failure{std::format("cannot write the bindings file {}", path)};
    }
    return true;
}

} // namespace coney::input
