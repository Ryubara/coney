// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/attack_kinds.h"

#include <algorithm>
#include <cstdint>

#include "animation/anim_clip.h"
#include "combat/anim_ids.h"

namespace coney::ai {

namespace {

// The clips' event frames are 1/30 s apart (docs/research/formats/animation.md).
constexpr int kEventFramesPerSecond = 30;

// The command of each kind (docs/research/ai.md#attack-kinds); kinds 24 and 35, square or cross at random, are -1.
constexpr std::array<int, kAttackKinds> kCommands{0x10, 0x0f, 0x12, 0x12, 0x11, 0x11, 0x12, 0x11, 0x14, 0x13, // 0-9
                                                  0x0f, 0x36, 0x37, 0x38, 0x0e, 0x24, 0x22, 0x23, 0,    0x20, // 10-19
                                                  0x21, 0x0e, 0x0d, 0x39, -1,   0x0d, 0x22, 0x11, 0x11, 0x0d, // 20-29
                                                  0x0d, 0x0f, 0x10, 0x0d, 0x19, -1,   0x05, 0x22, 0x11, 0x11, // 30-39
                                                  0x0d, 0x31, 0x0f, 0x10, 0x19};                              // 40-44

// Whether `command` is one Coney's dispatcher takes from an AI against a human: square, cross, and a chain's steps.
bool strikeCommand(int command) {
    return command == static_cast<int>(combat::command::kSquarePressed) ||
           command == static_cast<int>(combat::command::kCrossLongHold) ||
           command == static_cast<int>(combat::command::kSquareChain) ||
           command == static_cast<int>(combat::command::kCrossPressed) || command == -1;
}

} // namespace

combat::CommandId commandOf(int kind, combat::CombatRandom& random) {
    if (kind < 0 || kind >= static_cast<int>(kAttackKinds)) {
        return combat::command::kNone;
    }
    const int command = kCommands[static_cast<std::size_t>(kind)];
    if (command < 0) {
        return random.coin() ? combat::command::kSquarePressed : combat::command::kCrossLongHold;
    }
    return static_cast<combat::CommandId>(command);
}

std::vector<int> chainOf(int kind) {
    switch (kind) {
    case 2:
        return {0, 2};
    case 3:
        return {1, 3};
    case 4:
        return {0, 4};
    case 5:
        return {1, 5};
    case 6:
    case 7:
    case 8:
    case 9:
        return {1, 5, kind};
    default:
        return {kind};
    }
}

std::optional<std::uint32_t> chainDelayClip(int kind) {
    switch (kind) {
    case 0:
        return static_cast<std::uint32_t>(combat::anim_id::kAttackX1);
    case 1:
        return static_cast<std::uint32_t>(combat::anim_id::kAttackS1);
    case 5:
        return static_cast<std::uint32_t>(combat::anim_id::kAttackSS2);
    default:
        return std::nullopt;
    }
}

int chainDelayMs(int kind, const characters::AnimSet& anims) {
    const std::optional<std::uint32_t> id = chainDelayClip(kind);
    const anim::AnimClip* clip = id.has_value() ? anims.clip(*id) : nullptr;
    if (clip == nullptr || clip->events.empty()) {
        return 0;
    }
    const auto first = std::ranges::min_element(clip->events, {}, &anim::ClipEvent::frame);
    return static_cast<int>(first->frame) * 1000 / kEventFramesPerSecond;
}

std::uint32_t firstAnimOf(int kind) {
    const std::vector<int> chain = chainOf(kind);
    const int first = chain.empty() ? kind : chain.front();
    const bool cross = first >= 0 && first < static_cast<int>(kAttackKinds) &&
                       kCommands[static_cast<std::size_t>(first)] == static_cast<int>(combat::command::kCrossLongHold);
    return static_cast<std::uint32_t>(cross ? combat::anim_id::kAttackX1 : combat::anim_id::kAttackS1);
}

bool playable(int kind) {
    if (kind < 0 || kind >= static_cast<int>(kAttackKinds) || kind == 8 || kind == 9) {
        return false;
    }
    const std::vector<int> chain = chainOf(kind);
    // A chain must start with a press that starts an attack on its own (square or cross), and every step must be one
    // the chain takes.
    const int first = kCommands[static_cast<std::size_t>(chain.front())];
    if (first == static_cast<int>(combat::command::kSquareChain) ||
        first == static_cast<int>(combat::command::kCrossPressed)) {
        return false;
    }
    return std::ranges::all_of(chain,
                               [](int step) { return strikeCommand(kCommands[static_cast<std::size_t>(step)]); });
}

std::optional<int> pickAttack(const AttackWeights& weights, combat::CombatRandom& random) {
    int total = 0;
    for (int kind = 0; kind < static_cast<int>(kAttackKinds); ++kind) {
        total += playable(kind) ? weights[static_cast<std::size_t>(kind)] : 0;
    }
    if (total == 0) {
        return std::nullopt;
    }
    // One draw in [0, total), then the kind whose share of the running sum holds it.
    int draw = rollRange(random, 0, total - 1);
    for (int kind = 0; kind < static_cast<int>(kAttackKinds); ++kind) {
        const int weight = playable(kind) ? weights[static_cast<std::size_t>(kind)] : 0;
        if (draw < weight) {
            return kind;
        }
        draw -= weight;
    }
    return std::nullopt;
}

AttackWeights attNormal() {
    AttackWeights weights{};
    // docs/research/ai.md#level99, by kind (the table's Lua index - 1).
    const auto set = [&weights](int first, int last, std::uint8_t weight) {
        for (int kind = first; kind <= last; ++kind) {
            weights[static_cast<std::size_t>(kind)] = weight;
        }
    };
    set(0, 1, 10);
    set(2, 5, 30);
    set(6, 7, 60);
    set(10, 10, 40);
    set(12, 14, 30);
    set(19, 20, 30);
    set(21, 22, 55);
    set(24, 24, 50);
    set(25, 25, 75);
    set(28, 28, 40);
    set(29, 30, 60);
    set(31, 31, 100);
    set(33, 35, 50);
    set(36, 36, 10);
    set(39, 39, 30);
    set(40, 40, 65);
    set(42, 42, 200);
    set(43, 44, 100);
    return weights;
}

AttackWeights attBum() {
    AttackWeights weights{};
    weights[1] = 10;
    weights[5] = 10;
    weights[7] = 20;
    return weights;
}

int rollRange(combat::CombatRandom& random, int low, int high) {
    if (high <= low) {
        return low;
    }
    const auto span = static_cast<std::uint32_t>(high - low + 1);
    // The high bits: the generator's low bits are weak (combat::CombatRandom).
    return low + static_cast<int>((random.next() >> 8U) % span);
}

} // namespace coney::ai
