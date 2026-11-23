// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/input_recording.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "core/input_script.h"

namespace coney {

namespace {

// The bits of the pad's button word.
constexpr unsigned kButtonBits = 16;

// The names of the buttons in `mask`, in bit order, each after a space (` cross square`).
std::string buttonNames(std::uint16_t mask) {
    std::string names;
    for (unsigned bit = 0; bit < kButtonBits; ++bit) {
        const auto flag = static_cast<std::uint16_t>(1U << bit);
        if ((mask & flag) != 0) {
            names += ' ';
            names += inputScriptButtonName(flag);
        }
    }
    return names;
}

// The line that moves one stick to bytes `x` and `y` (y down, as the pad has it): `stick` when a script's values give
// those bytes and `rawstick` when only a real pad's do.
std::string stickLine(std::string_view side, std::uint8_t x, std::uint8_t y) {
    const std::optional<int> percentX = percentFromStickByte(x);
    const std::optional<int> percentY = percentFromStickByte(y);
    if (percentX && percentY) {
        // A script's y is up, the byte's is down.
        return std::format("stick {} {} {}", side, *percentX, -*percentY);
    }
    return std::format("rawstick {} {} {}", side, x, y);
}

// Whether a name is safe to write into a header: letters, digits and `_ : . -` only, so it can name a level but never
// carry a path or text.
bool plainName(std::string_view name) {
    return !name.empty() && std::ranges::all_of(name, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == ':' ||
               c == '.' || c == '-';
    });
}

} // namespace

InputRecorder::InputRecorder() {
    // The state a script starts from: port 1 plugged in, everything at rest.
    m_ports.at(0).pad.connected = true;
}

void InputRecorder::recordPort(std::string& out, std::uint64_t frame, std::size_t port, const PadSample& sample) {
    Port& state = m_ports.at(port);
    const std::string prefix = port == 0 ? std::format("{} ", frame) : std::format("{} p2 ", frame);
    const auto emit = [&out, &prefix](std::string_view line) {
        out += prefix;
        out += line;
        out += '\n';
    };

    if (sample.connected != state.pad.connected) {
        emit(sample.connected ? "connect" : "disconnect");
        state.pad.connected = sample.connected;
    }
    if (!sample.connected) {
        // The rest of an unplugged pad's sample is ignored by the game; the replay keeps what it had.
        return;
    }

    // Buttons: lets go first, then presses.
    if (const auto released = static_cast<std::uint16_t>(state.pad.buttons & ~sample.buttons); released != 0) {
        emit("release" + buttonNames(released));
    }
    if (const auto pressed = static_cast<std::uint16_t>(sample.buttons & ~state.pad.buttons); pressed != 0) {
        emit("press" + buttonNames(pressed));
    }
    state.pad.buttons = sample.buttons;

    // Sticks: the pad's bytes are right x, right y, left x, left y.
    if (sample.sticks[2] != state.pad.sticks[2] || sample.sticks[3] != state.pad.sticks[3]) {
        emit(stickLine("left", sample.sticks[2], sample.sticks[3]));
        state.pad.sticks[2] = sample.sticks[2];
        state.pad.sticks[3] = sample.sticks[3];
    }
    if (sample.sticks[0] != state.pad.sticks[0] || sample.sticks[1] != state.pad.sticks[1]) {
        emit(stickLine("right", sample.sticks[0], sample.sticks[1]));
        state.pad.sticks[0] = sample.sticks[0];
        state.pad.sticks[1] = sample.sticks[1];
    }

    // Pressure: a byte that is not what the button's state gives (255 held, 0 released) is a `pressure` line, and
    // goes back to `auto` when it is again.
    for (std::size_t i = 0; i < pad::kPressureCount; ++i) {
        const std::uint16_t bit = pad::kPressureButtons.at(i);
        const std::uint8_t derived = (sample.buttons & bit) != 0 ? 255 : 0;
        const std::uint8_t replayed = state.pressure.at(i).value_or(derived);
        const std::uint8_t wanted = sample.pressure.at(i);
        if (wanted == replayed) {
            continue;
        }
        if (wanted == derived) {
            emit(std::format("pressure {} auto", inputScriptButtonName(bit)));
            state.pressure.at(i).reset();
        } else {
            emit(std::format("pressure {} {}", inputScriptButtonName(bit), wanted));
            state.pressure.at(i) = wanted;
        }
    }
}

std::string InputRecorder::record(std::uint64_t frame, const PortSamples& samples) {
    std::string out;
    for (std::size_t port = 0; port < kPadPorts; ++port) {
        recordPort(out, frame, port, samples.at(port));
    }
    return out;
}

std::string inputRecordingHeader(const InputRecordingHeader& header) {
    std::string text = "# coney input recording 1: play it with --input-script\n";
    text += std::format("# ai-seed {}\n", header.aiSeed);
    if (plainName(header.level)) {
        text += std::format("# play-level {}\n", header.level);
    }
    if (header.checkpoint) {
        text += std::format("# checkpoint {}\n", *header.checkpoint);
    }
    return text;
}

RecordingInput::RecordingInput(InputSource& inner, Sink sink, std::function<bool()> started)
    : m_inner(inner), m_sink(std::move(sink)), m_started(std::move(started)) {}

RecordingInput::~RecordingInput() {
    if (m_origin && m_sink) {
        m_sink(std::format("# frames {}\n", m_recorded));
    }
}

PortSamples RecordingInput::sample(std::uint64_t frame) {
    // Before the start nothing is recorded, and what `inner` gives goes on.
    if (!m_origin && (!m_started || m_started())) {
        m_origin = frame;
    }
    const PortSamples samples = m_inner.sample(frame);
    if (m_origin) {
        const std::string lines = m_recorder.record(frame - *m_origin, samples);
        if (!lines.empty() && m_sink) {
            m_sink(lines);
        }
        ++m_recorded;
    }
    return samples;
}

} // namespace coney
