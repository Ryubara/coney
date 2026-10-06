// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/ai_config.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <span>

#include "scripting/lua_value.h"

namespace coney::ai {

namespace {

// `CfgChar`'s arguments (docs/references/bindings/config.md#cfgchar), 0-based.
constexpr std::size_t kCharBehaviour = 1;
constexpr std::size_t kCharHealth = 5;
constexpr std::size_t kCharDamage = 6;
constexpr std::size_t kCharAttacks = 7;
constexpr std::size_t kCharDamageScale = 8;
// `CfgPowerClass`'s arguments (docs/references/bindings/config.md#cfgpowerclass), 0-based, and the fields they fill.
constexpr std::size_t kPowerMax = 1;         // +0x28
constexpr std::size_t kPowerRefill = 2;      // +0x2a
constexpr std::size_t kGroundMs = 5;         // +0x34
constexpr std::size_t kStunMs = 6;           // +0x30
constexpr std::size_t kHurt = 9;             // +0x04
constexpr std::size_t kBlock = 10;           // +0x08
constexpr std::size_t kHurtBlock = 11;       // +0x0c
constexpr std::size_t kHurtPower = 14;       // +0x18
constexpr std::size_t kDelayFactor = 15;     // +0x1c
constexpr std::size_t kDelayDownFactor = 16; // +0x20
constexpr std::size_t kStruggle = 17;        // +0x36
constexpr std::size_t kCounter = 19;         // +0x24, clamped 0-1

// Argument `index` of `call` as a number, if it is one.
std::optional<double> numberAt(std::span<const script::Value> call, std::size_t index) {
    return index < call.size() ? call[index].number() : std::nullopt;
}

// The last call of `binding` whose first argument is `id`; empty when none.
std::span<const script::Value> lastCallOf(const script::RecordedCalls& recorded, std::string_view binding, int id) {
    std::span<const script::Value> found;
    for (const std::vector<script::Value>& call : recorded.calls(binding)) {
        if (numberAt(call, 0) == static_cast<double>(id)) {
            found = call;
        }
    }
    return found;
}

// The numbers of the list kept for argument `index` of `call` (script::RecordedCalls keeps a table as such a list).
std::vector<double> listAt(std::span<const script::Value> call, std::size_t index) {
    std::vector<double> numbers;
    if (index >= call.size() || call[index].table() == nullptr) {
        return numbers;
    }
    for (double key = 1.0;; key += 1.0) {
        const std::optional<double> number = call[index].table()->get(script::Value(key)).number();
        if (!number.has_value()) {
            return numbers;
        }
        numbers.push_back(*number);
    }
}

// The fighter class from type `type`'s `CfgChar` call.
void readCharacter(std::span<const script::Value> call, AiConfig& config) {
    if (call.empty()) {
        return;
    }
    ++config.callsRead;
    if (const auto behaviour = numberAt(call, kCharBehaviour)) {
        config.fighter.brain = brainTypeOf(static_cast<int>(*behaviour));
    }
    if (const auto health = numberAt(call, kCharHealth); health.has_value() && *health >= 1.0) {
        config.fighter.health = static_cast<int>(*health);
    }
    const std::vector<double> attacks = listAt(call, kCharAttacks);
    if (attacks.size() >= kAttackKinds) {
        for (std::size_t kind = 0; kind < kAttackKinds; ++kind) {
            config.settings.attackWeights[kind] = static_cast<std::uint8_t>(std::clamp(attacks[kind], 0.0, 255.0));
        }
    }
    // The damage table times the class's scale, as 16-bit values (truncated: **Coney choice**).
    const double scale = numberAt(call, kCharDamageScale).value_or(1.0);
    config.fighter.damage.clear();
    for (const double value : listAt(call, kCharDamage)) {
        config.fighter.damage.push_back(
            static_cast<std::int16_t>(std::clamp(std::trunc(value * scale), -32768.0, 32767.0)));
    }
}

// The power class's fields from its `CfgPowerClass` call.
void readPowerClass(std::span<const script::Value> call, AiConfig& config) {
    if (call.empty()) {
        return;
    }
    ++config.callsRead;
    combat::PowerClass& power = config.powerClass;
    const auto whole = [&call](std::size_t index, int& field) {
        if (const auto value = numberAt(call, index)) {
            field = static_cast<int>(*value);
        }
    };
    const auto real = [&call](std::size_t index, float& field) {
        if (const auto value = numberAt(call, index)) {
            field = static_cast<float>(*value);
        }
    };
    whole(kPowerMax, power.powerMax);
    whole(kPowerRefill, power.refillPerSecond);
    whole(kGroundMs, power.groundMs);
    whole(kStunMs, power.stunMs);
    whole(kStruggle, power.struggleDivisor);
    power.struggleDivisor = std::max(1, power.struggleDivisor);
    real(kHurt, power.hurtFraction);
    real(kBlock, power.blockChance);
    real(kHurtBlock, power.hurtBlockChance);
    real(kHurtPower, power.hurtPowerFactor);
    real(kDelayFactor, power.attackDelayFactor);
    real(kDelayDownFactor, power.attackDelayDownFactor);
    real(kCounter, power.counterChance);
    power.counterChance = std::clamp(power.counterChance, 0.0F, 1.0F);
}

} // namespace

AiConfig aiConfigFrom(const script::RecordedCalls& recorded, int type, int powerClass) {
    AiConfig config;
    config.fighter.type = type;
    readCharacter(lastCallOf(recorded, "CfgChar", type), config);
    readPowerClass(lastCallOf(recorded, "CfgPowerClass", powerClass), config);
    // CfgAttackDelay(attack, ms): one entry of the global table each.
    for (const std::vector<script::Value>& call : recorded.calls("CfgAttackDelay")) {
        const auto kind = numberAt(call, 0);
        const auto ms = numberAt(call, 1);
        if (kind.has_value() && ms.has_value() && *kind >= 0.0 && *kind < static_cast<double>(kAttackKinds)) {
            config.settings.attackDelaysMs[static_cast<std::size_t>(*kind)] = static_cast<int>(*ms);
            ++config.callsRead;
        }
    }
    // CfgBaseChanceToBlock(percent): values above 100 are ignored.
    for (const std::vector<script::Value>& call : recorded.calls("CfgBaseChanceToBlock")) {
        if (const auto percent = numberAt(call, 0); percent.has_value() && *percent >= 0.0 && *percent <= 100.0) {
            config.settings.baseBlockChance = static_cast<int>(*percent);
            ++config.callsRead;
        }
    }
    return config;
}

} // namespace coney::ai
