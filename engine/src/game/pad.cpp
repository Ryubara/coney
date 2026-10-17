#include "game/pad.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace coney::game {

// The stick table: 0 and 255 are the ends, 95 to 160 the dead zone, and the bytes between go in steps of 1/95.
// @orig 0x001454a8 Read both controllers each update
// @page research/pages/pad-input/page.md
float stick_axis_value(std::uint8_t byte) noexcept {
    constexpr float kStep = 95.0f;
    if (byte == 0) {
        return -1.0f;
    }
    if (byte == 255) {
        return 1.0f;
    }
    if (byte < kDeadZoneLow) {
        return static_cast<float>(byte - kDeadZoneLow) / kStep;
    }
    if (byte > kDeadZoneHigh) {
        return static_cast<float>(byte - kDeadZoneHigh) / kStep;
    }
    return 0.0f;
}

// Each axis goes through the table, the vertical ones sign-flipped so up is positive; the right stick also ignores
// its wider dead zone. Each stick then gets its direction and its length, capped at 1.
// @orig 0x001454a8 Read both controllers each update
// @page research/pages/pad-input/page.md
PlayerSticks convert_sticks(const PadSticks& sticks) noexcept {
    const auto finish = [](float x, float y) {
        StickValue value{.x = x, .y = y};
        const float length = std::hypot(x, y);
        if (length > 0.0f) {
            value.direction_x = x / length;
            value.direction_y = y / length;
        }
        value.length = std::min(length, 1.0f);
        return value;
    };
    const auto outside = [](std::uint8_t byte, std::uint8_t low, std::uint8_t high) {
        return byte < low || byte > high;
    };

    PlayerSticks out;
    out.left = finish(stick_axis_value(sticks.left.x), -stick_axis_value(sticks.left.y));

    const std::uint8_t rx = sticks.right.x;
    const std::uint8_t ry = sticks.right.y;
    const float right_x = outside(rx, kRightDeadZoneXLow, kRightDeadZoneXHigh) ? stick_axis_value(rx) : 0.0f;
    const float right_y = outside(ry, kRightDeadZoneYLow, kRightDeadZoneYHigh) ? -stick_axis_value(ry) : 0.0f;
    out.right = finish(right_x, right_y);
    return out;
}

// The d-pad gives one direction only: the one held longest, the lowest bit when they went down together. The word
// then joins the history and every enabled binding is tried; the matching binding of lowest rank is carried, the
// first listed when two of one rank match.
// @orig 0x00146078 Turn a player's pad into stick values and a command
// @page research/pages/pad-input/page.md
std::optional<int> update_pad_commands(PlayerPad& pad, ButtonWord buttons, std::span<const CommandBinding> bindings) {
    constexpr int kCountCap = std::numeric_limits<int>::max() / 2;
    PadHistory& history = pad.history;

    ButtonWord direction = 0;
    int longest = 0;
    for (int bit = 0; bit < kButtonCount; ++bit) {
        const auto mask = static_cast<ButtonWord>(1u << bit);
        if ((button::kDpad & mask) == 0) {
            continue;
        }
        int& count = history.raw_held_updates[static_cast<std::size_t>(bit)];
        count = (buttons & mask) != 0 ? std::min(count + 1, kCountCap) : 0;
        if (count > longest) {
            longest = count;
            direction = mask;
        }
    }
    const auto word = static_cast<ButtonWord>((buttons & ~button::kDpad) | direction);

    const std::array<int, kButtonCount> before = history.held_updates;
    const ButtonWord previous = history.words[0];
    std::copy_backward(history.words.begin(), history.words.end() - 1, history.words.end());
    history.words[0] = word;
    for (std::size_t bit = 0; bit < history.held_updates.size(); ++bit) {
        int& count = history.held_updates[bit];
        count = (word & (1u << bit)) != 0 ? std::min(count + 1, kCountCap) : 0;
    }

    const auto all_down = [](ButtonWord in, ButtonWord group) { return group != 0 && (in & group) == group; };
    // How many updates in a row every button of the group has been down.
    const auto run = [](const std::array<int, kButtonCount>& counts, ButtonWord group) {
        int shortest = group != 0 ? kCountCap : 0;
        for (std::size_t bit = 0; bit < counts.size(); ++bit) {
            if ((group & (1u << bit)) != 0) {
                shortest = std::min(shortest, counts[bit]);
            }
        }
        return shortest;
    };
    const auto matches = [&](const CommandBinding& binding) {
        const ButtonWord group = binding.buttons;
        const bool down = all_down(word, group);
        const bool was_down = all_down(previous, group);
        const int held = run(history.held_updates, group);
        const int held_before = run(before, group);
        switch (binding.kind) {
        case TriggerKind::Held:
            return down;
        case TriggerKind::Pressed:
            return down && !was_down;
        case TriggerKind::Released:
            return !down && was_down;
        case TriggerKind::Repeat:
            return (down && !was_down) || (down && (group & ~button::kDpad) == 0 && held >= kRepeatFirst &&
                                           (held - kRepeatFirst) % kRepeatEvery == 0);
        case TriggerKind::Tapped:
            return !down && was_down && held_before <= kTapMostUpdates;
        case TriggerKind::LongHold:
            return (!down && was_down && held_before < kLongHoldUpdates) || held == kLongHoldUpdates;
        case TriggerKind::HistoryHold:
            return held >= kHistoryHoldLength && held < kHistoryLength;
        case TriggerKind::CombinationHeld: {
            const auto both = static_cast<ButtonWord>(group | binding.second);
            return binding.second != 0 && all_down(word, both) && !all_down(previous, both);
        }
        case TriggerKind::CombinationPress:
            return down && all_down(word, binding.second) && !all_down(previous, binding.second);
        }
        return false;
    };

    std::optional<int> carried;
    int carried_rank = 0;
    for (const CommandBinding& binding : bindings) {
        const bool off = binding.command >= 0 && binding.command <= kMostCommandId &&
                         pad.switched_off.test(static_cast<std::size_t>(binding.command));
        if (off || !matches(binding)) {
            continue;
        }
        const int rank = kTriggerRank[static_cast<std::size_t>(binding.kind)];
        if (!carried || rank < carried_rank) {
            carried = binding.command;
            carried_rank = rank;
        }
    }
    return carried;
}

} // namespace coney::game
