// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/input_script.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "core/assert.h"
#include "core/pad.h"

namespace coney {

namespace {

// A button's name in a script and its bit.
struct ButtonName {
    std::string_view name;
    std::uint16_t bit;
};

// Every button a script can name, by the names on the PS2 pad.
constexpr std::array<ButtonName, 16> kButtonNames{{
    {"l2", pad::kL2},
    {"r2", pad::kR2},
    {"l1", pad::kL1},
    {"r1", pad::kR1},
    {"triangle", pad::kTriangle},
    {"circle", pad::kCircle},
    {"cross", pad::kCross},
    {"square", pad::kSquare},
    {"select", pad::kSelect},
    {"l3", pad::kL3},
    {"r3", pad::kR3},
    {"start", pad::kStart},
    {"up", pad::kUp},
    {"right", pad::kRight},
    {"down", pad::kDown},
    {"left", pad::kLeft},
}};

// Splits a line into its words, after cutting off a `#` comment.
std::vector<std::string_view> splitWords(std::string_view line) {
    if (const std::size_t hash = line.find('#'); hash != std::string_view::npos) {
        line = line.substr(0, hash);
    }
    std::vector<std::string_view> words;
    std::size_t at = 0;
    while (at < line.size()) {
        const std::size_t start = line.find_first_not_of(" \t\r", at);
        if (start == std::string_view::npos) {
            break;
        }
        const std::size_t end = std::min(line.find_first_of(" \t\r", start), line.size());
        words.push_back(line.substr(start, end - start));
        at = end;
    }
    return words;
}

// Parses a whole word as a number of type T; nothing else may follow the digits.
template <typename T> std::optional<T> parseNumber(std::string_view word) {
    T value{};
    const auto parsed = std::from_chars(word.data(), word.data() + word.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != word.data() + word.size()) {
        return std::nullopt;
    }
    return value;
}

// The bit of one button name, or nullopt for an unknown name.
std::optional<std::uint16_t> buttonBit(std::string_view name) {
    for (const ButtonName& button : kButtonNames) {
        if (button.name == name) {
            return button.bit;
        }
    }
    return std::nullopt;
}

// Parses the arguments of one line after its action word into `event`; returns what is wrong, or an empty string.
std::string parseArguments(std::span<const std::string_view> args, InputEvent& event) {
    switch (event.action) {
    case InputEvent::Action::Press:
    case InputEvent::Action::Release:
    case InputEvent::Action::Tap:
        if (args.empty()) {
            return "needs at least one button";
        }
        for (const std::string_view name : args) {
            const std::optional<std::uint16_t> bit = buttonBit(name);
            if (!bit) {
                return std::format("unknown button \"{}\"", name);
            }
            event.buttons |= *bit;
        }
        return {};
    case InputEvent::Action::Stick: {
        if (args.size() != 3 || (args[0] != "left" && args[0] != "right")) {
            return "stick needs left or right, then X and Y";
        }
        event.stick = args[0] == "left" ? 0 : 1;
        const std::optional<int> x = parseNumber<int>(args[1]);
        const std::optional<int> y = parseNumber<int>(args[2]);
        if (!x || !y || *x < -100 || *x > 100 || *y < -100 || *y > 100) {
            return "stick X and Y must be whole numbers from -100 to 100";
        }
        event.x = *x;
        event.y = *y;
        return {};
    }
    case InputEvent::Action::Connect:
    case InputEvent::Action::Disconnect:
        return args.empty() ? std::string() : "takes no arguments";
    }
    return "unknown action";
}

// Parses one non-empty line's words into an event; returns what is wrong, or an empty string.
std::string parseLine(std::span<const std::string_view> words, InputEvent& event) {
    const std::optional<std::uint64_t> frame = parseNumber<std::uint64_t>(words[0]);
    if (!frame) {
        return std::format("\"{}\" is not a frame number", words[0]);
    }
    event.frame = *frame;
    std::size_t at = 1;
    // The optional port.
    if (at < words.size() && (words[at] == "p1" || words[at] == "p2")) {
        event.port = words[at] == "p1" ? 0 : 1;
        ++at;
    }
    if (at == words.size()) {
        return "needs an action after the frame";
    }
    const std::string_view action = words[at++];
    if (action == "press") {
        event.action = InputEvent::Action::Press;
    } else if (action == "release") {
        event.action = InputEvent::Action::Release;
    } else if (action == "tap") {
        event.action = InputEvent::Action::Tap;
    } else if (action == "stick") {
        event.action = InputEvent::Action::Stick;
    } else if (action == "connect") {
        event.action = InputEvent::Action::Connect;
    } else if (action == "disconnect") {
        event.action = InputEvent::Action::Disconnect;
    } else {
        return std::format("unknown action \"{}\"", action);
    }
    return parseArguments(words.subspan(at), event);
}

} // namespace

std::expected<std::vector<InputEvent>, Error> parseInputScript(std::string_view text) {
    std::vector<InputEvent> events;
    std::size_t lineNumber = 0;
    while (!text.empty()) {
        // Take the next line.
        const std::size_t newline = text.find('\n');
        const std::string_view line = text.substr(0, newline);
        text = newline == std::string_view::npos ? std::string_view() : text.substr(newline + 1);
        ++lineNumber;

        const std::vector<std::string_view> words = splitWords(line);
        if (words.empty()) {
            continue;
        }
        InputEvent event;
        if (std::string problem = parseLine(words, event); !problem.empty()) {
            return fail(ErrorCode::Invalid, std::format("input script line {}: {}", lineNumber, problem));
        }
        // Frames in order keep a script readable as a timeline, and let the player apply it in one pass.
        if (!events.empty() && event.frame < events.back().frame) {
            return fail(ErrorCode::Invalid, std::format("input script line {}: frame {} comes after frame {}",
                                                        lineNumber, event.frame, events.back().frame));
        }
        events.push_back(event);
    }
    return events;
}

std::expected<std::vector<InputEvent>, Error> loadInputScript(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return fail(ErrorCode::NotFound, std::format("cannot open the input script {}", path));
    }
    const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    if (file.bad()) {
        return fail(ErrorCode::Io, std::format("cannot read the input script {}", path));
    }
    auto events = parseInputScript(text);
    if (!events) {
        return fail(events.error().code, std::format("{}: {}", path, events.error().message));
    }
    return events;
}

std::uint8_t stickByteFromPercent(int value) {
    CONEY_ASSERT(value >= -100 && value <= 100);
    // 95 steps on each side of the dead zone, rounded to the nearest.
    constexpr int kSpan = 95;
    if (value > 0) {
        return static_cast<std::uint8_t>(pad::kStickDeadHigh + (value * kSpan + 50) / 100);
    }
    if (value < 0) {
        return static_cast<std::uint8_t>(pad::kStickDeadLow - (-value * kSpan + 50) / 100);
    }
    return pad::kStickCentre;
}

ScriptedInput::ScriptedInput(std::vector<InputEvent> events) : m_events(std::move(events)) {
    for (std::size_t i = 1; i < m_events.size(); ++i) {
        CONEY_ASSERT(m_events[i - 1].frame <= m_events[i].frame);
    }
    m_state[0].connected = true;
}

void ScriptedInput::apply(const InputEvent& event) {
    CONEY_ASSERT(event.port < kPadPorts);
    PadSample& port = m_state.at(event.port);
    switch (event.action) {
    case InputEvent::Action::Press:
        port.buttons |= event.buttons;
        break;
    case InputEvent::Action::Release:
        port.buttons &= static_cast<std::uint16_t>(~event.buttons);
        break;
    case InputEvent::Action::Tap:
        port.buttons |= event.buttons;
        m_tapped.at(event.port) |= event.buttons;
        break;
    case InputEvent::Action::Stick: {
        // Raw stick bytes are right x, right y, left x, left y; a script's y is up, the byte's is down.
        const std::size_t first = event.stick == 0 ? 2 : 0;
        port.sticks.at(first) = stickByteFromPercent(event.x);
        port.sticks.at(first + 1) = stickByteFromPercent(-event.y);
        break;
    }
    case InputEvent::Action::Connect:
        port.connected = true;
        break;
    case InputEvent::Action::Disconnect:
        port.connected = false;
        break;
    }
}

PortSamples ScriptedInput::sample(std::uint64_t frame) {
    CONEY_ASSERT(frame >= m_nextFrame);
    m_nextFrame = frame + 1;

    // Taps of the frame before end now, before this frame's lines, so a tap on two frames in a row stays held.
    for (std::size_t port = 0; port < kPadPorts; ++port) {
        m_state.at(port).buttons &= static_cast<std::uint16_t>(~m_tapped.at(port));
        m_tapped.at(port) = 0;
    }
    while (m_next < m_events.size() && m_events[m_next].frame <= frame) {
        apply(m_events[m_next++]);
    }

    // A digital button is either released (0) or fully pressed (255).
    PortSamples samples = m_state;
    for (PadSample& sample : samples) {
        for (std::size_t i = 0; i < pad::kPressureCount; ++i) {
            sample.pressure.at(i) = (sample.buttons & pad::kPressureButtons.at(i)) != 0 ? 255 : 0;
        }
    }
    return samples;
}

} // namespace coney
