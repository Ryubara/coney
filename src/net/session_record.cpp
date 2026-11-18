// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/session_record.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

#include "core/assert.h"

namespace coney::net {

namespace {

constexpr std::string_view kPadWord = "pad";
constexpr std::string_view kEventWord = "event";

// The next space-separated word of `rest`, taken off its front; empty at the end.
std::string_view takeWord(std::string_view& rest) {
    const std::size_t space = rest.find(' ');
    const std::string_view word = rest.substr(0, space);
    rest = space == std::string_view::npos ? std::string_view{} : rest.substr(space + 1);
    return word;
}

// A decimal step, digits only, that fits 32 bits.
std::optional<std::uint32_t> readStep(std::string_view word) {
    if (word.empty() || !std::ranges::all_of(word, [](char c) { return c >= '0' && c <= '9'; })) {
        return std::nullopt;
    }
    std::uint32_t value = 0;
    const auto [end, error] = std::from_chars(word.data(), word.data() + word.size(), value);
    if (error != std::errc{} || end != word.data() + word.size()) {
        return std::nullopt;
    }
    return value;
}

// Exactly `digits` lowercase hex digits as a number.
std::optional<std::uint64_t> readHex(std::string_view word, std::size_t digits) {
    const auto lowerHex = [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); };
    if (word.size() != digits || !std::ranges::all_of(word, lowerHex)) {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    std::from_chars(word.data(), word.data() + word.size(), value, 16);
    return value;
}

// `bytes.size()` bytes from 2 lowercase hex digits each.
bool readBytes(std::string_view word, std::span<std::uint8_t> bytes) {
    if (word.size() != bytes.size() * 2) {
        return false;
    }
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        const auto byte = readHex(word.substr(i * 2, 2), 2);
        if (!byte) {
            return false;
        }
        bytes[i] = static_cast<std::uint8_t>(*byte);
    }
    return true;
}

// Bytes as 2 lowercase hex digits each.
std::string hexBytes(std::span<const std::uint8_t> bytes) {
    std::string text;
    for (const std::uint8_t byte : bytes) {
        text += std::format("{:02x}", byte);
    }
    return text;
}

} // namespace

std::string formatPadLine(std::uint32_t step, const PadSample& pad) {
    if (!pad.connected) {
        return std::format("{} {} 0", kPadWord, step);
    }
    return std::format("{} {} 1 {:04x} {} {}", kPadWord, step, pad.buttons, hexBytes(pad.sticks),
                       hexBytes(pad.pressure));
}

std::string formatEventLine(std::uint32_t step, std::string_view text) {
    CONEY_ASSERT(!text.empty() && text.find('\n') == std::string_view::npos);
    return std::format("{} {} {}", kEventWord, step, text);
}

std::expected<RecordLine, Error> parseRecordLine(std::string_view text) {
    const auto invalid = [text] {
        return std::unexpected(Error{ErrorCode::Invalid, std::format("not a session record line: '{}'", text)});
    };
    std::string_view rest = text;
    const std::string_view kind = takeWord(rest);
    const auto step = readStep(takeWord(rest));
    if (!step) {
        return invalid();
    }
    RecordLine line;
    line.step = *step;
    if (kind == kEventWord) {
        if (rest.empty()) {
            return invalid();
        }
        line.kind = RecordLine::Kind::Event;
        line.text = std::string(rest);
        return line;
    }
    if (kind != kPadWord) {
        return invalid();
    }
    line.kind = RecordLine::Kind::Pad;
    const std::string_view connected = takeWord(rest);
    if (connected == "0" && rest.empty()) {
        return line; // unplugged: nothing more
    }
    if (connected != "1") {
        return invalid();
    }
    const auto buttons = readHex(takeWord(rest), 4);
    const bool sticks = readBytes(takeWord(rest), line.pad.sticks);
    const bool pressures = readBytes(takeWord(rest), line.pad.pressure);
    if (!buttons || !sticks || !pressures || !rest.empty()) {
        return invalid();
    }
    line.pad.connected = true;
    line.pad.buttons = static_cast<std::uint16_t>(*buttons);
    return line;
}

std::expected<SessionReplay, Error> SessionReplay::parse(std::string_view text) {
    SessionReplay replay;
    std::size_t number = 0;
    while (!text.empty()) {
        ++number;
        const std::size_t end = text.find('\n');
        std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        if (line.ends_with('\r')) {
            line.remove_suffix(1);
        }
        if (line.empty()) {
            continue;
        }
        auto read = parseRecordLine(line);
        if (!read) {
            return std::unexpected(Error{ErrorCode::Invalid, std::format("line {}: {}", number, read.error().message)});
        }
        if (read->kind != RecordLine::Kind::Pad) {
            continue; // a note of the session; the pads alone change the game
        }
        if (!replay.m_pads.emplace(read->step, read->pad).second) {
            return std::unexpected(
                Error{ErrorCode::Invalid, std::format("line {}: step {} is recorded twice", number, read->step)});
        }
    }
    return replay;
}

std::optional<PadSample> SessionReplay::pad(std::uint32_t step) const {
    const auto found = m_pads.find(step);
    if (found == m_pads.end()) {
        return std::nullopt;
    }
    return found->second;
}

} // namespace coney::net
