// SPDX-License-Identifier: GPL-3.0-or-later
#include "characters/character_types.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "characters/character_class.h"

namespace coney::characters {

namespace {

// `CfgChar`'s arguments (docs/references/bindings/config.md#cfgchar), 0-based.
constexpr std::size_t kType = 0;
constexpr std::size_t kBehaviour = 1;
constexpr std::size_t kCategory = 2;
constexpr std::size_t kSpeedClass = 3;
constexpr std::size_t kPowerClass = 4;
constexpr std::size_t kHealth = 5;
constexpr std::size_t kDamage = 6;
constexpr std::size_t kAttacks = 7;
constexpr std::size_t kDamageScale = 8;
constexpr std::size_t kHat = 10;        // the string at +0x14c
constexpr std::size_t kMaxHatName = 31; // the most of it kept
constexpr std::size_t kWarrior = 11;
constexpr std::size_t kFemale = 12;

// `CfgPowerClass`'s arguments (docs/references/bindings/config.md#cfgpowerclass), 0-based, and the fields they fill.
constexpr std::size_t kPowerMax = 1;         // +0x28
constexpr std::size_t kPowerRefill = 2;      // +0x2a
constexpr std::size_t kStaminaMax = 3;       // +0x2c
constexpr std::size_t kStaminaRefill = 4;    // +0x2e
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
constexpr std::size_t kThrowsAtWalls = 21;   // +0x39, clamped 0-1
constexpr std::size_t kRingByte = 28;        // +0x40, clamped 0-100
constexpr int kMaxRingByte = 100;

// `CfgWarriorClass`'s arguments (docs/references/bindings/config.md#cfgwarriorclass), 0-based.
constexpr std::size_t kWarriorClassId = 0;
constexpr std::size_t kWarriorHealthPercent = 1; // byte +0x05
constexpr std::size_t kWarriorDamagePercent = 2; // byte +0x06
constexpr std::size_t kWarriorRageMax = 3;       // 16 bits at +0x00
constexpr std::size_t kWarriorRageGain = 4;      // byte +0x02
constexpr std::size_t kWarriorRageDecay = 5;     // byte +0x03
constexpr std::size_t kWarriorRageDrain = 6;     // byte +0x04
constexpr int kByteMax = 255;
constexpr int kU16Max = 65535;

// The entry for `key` in `entries` (kept sorted by key), replaced or inserted: a later call of a key wins.
template <typename Value> void putSorted(std::vector<std::pair<int, Value>>& entries, int key, Value value) {
    auto at = std::ranges::lower_bound(entries, key, {}, &std::pair<int, Value>::first);
    if (at != entries.end() && at->first == key) {
        at->second = std::move(value);
    } else {
        entries.insert(at, {key, std::move(value)});
    }
}

// The value for `key` in `entries` (sorted by key), or null.
template <typename Value> const Value* findSorted(const std::vector<std::pair<int, Value>>& entries, int key) {
    const auto at = std::ranges::lower_bound(entries, key, {}, &std::pair<int, Value>::first);
    return at != entries.end() && at->first == key ? &at->second : nullptr;
}

// Argument `index` of `call` as a number, if it is one.
std::optional<double> numberAt(std::span<const script::Value> call, std::size_t index) {
    return index < call.size() ? call[index].number() : std::nullopt;
}

// Argument `index` truncated to an int (the binding reads it so), clamped to the int range first.
std::optional<int> wholeAt(std::span<const script::Value> call, std::size_t index) {
    const std::optional<double> number = numberAt(call, index);
    if (!number || std::isnan(*number)) {
        return std::nullopt;
    }
    return static_cast<int>(std::trunc(std::clamp(*number, -2147483648.0, 2147483647.0)));
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

// Argument `index` of `call` as a record field of 0 to `maximum` (**Coney's choice**: clamped, which gives what the
// binding stores for every value the scripts pass), or nothing when it is not a number.
std::optional<int> fieldAt(std::span<const script::Value> call, std::size_t index, int maximum) {
    const std::optional<int> value = wholeAt(call, index);
    return value ? std::optional<int>(std::clamp(*value, 0, maximum)) : std::nullopt;
}

} // namespace

WarriorClass parseCfgWarriorClass(std::span<const script::Value> call) {
    WarriorClass parsed;
    parsed.healthPercent = fieldAt(call, kWarriorHealthPercent, kByteMax);
    parsed.damagePercent = fieldAt(call, kWarriorDamagePercent, kByteMax);
    // The rage needs all four: a call that leaves one out keeps the player's default.
    const std::optional<int> rageMax = fieldAt(call, kWarriorRageMax, kU16Max);
    const std::optional<int> gain = fieldAt(call, kWarriorRageGain, kByteMax);
    const std::optional<int> decay = fieldAt(call, kWarriorRageDecay, kByteMax);
    const std::optional<int> drain = fieldAt(call, kWarriorRageDrain, kByteMax);
    if (rageMax.has_value() && *rageMax > 0 && gain.has_value() && decay.has_value() && drain.has_value()) {
        parsed.rage = combat::RageClass{
            .maximum = *rageMax, .gainPercent = *gain, .decayPercent = *decay, .drainPercent = *drain};
    }
    return parsed;
}

std::optional<CharacterType> parseCfgChar(std::span<const script::Value> call) {
    const std::optional<int> type = wholeAt(call, kType);
    if (!type) {
        return std::nullopt;
    }
    CharacterType parsed;
    parsed.type = *type;
    parsed.behaviour = wholeAt(call, kBehaviour);
    parsed.category = wholeAt(call, kCategory);
    parsed.powerClass = wholeAt(call, kPowerClass);
    parsed.speedClass = wholeAt(call, kSpeedClass);
    parsed.health = wholeAt(call, kHealth);
    parsed.warrior = wholeAt(call, kWarrior);
    parsed.female = wholeAt(call, kFemale);
    // The damage table times the class's scale, as 16-bit values (truncated: **Coney choice**).
    const double scale = numberAt(call, kDamageScale).value_or(1.0);
    for (const double value : listAt(call, kDamage)) {
        parsed.damage.push_back(static_cast<std::int16_t>(std::clamp(std::trunc(value * scale), -32768.0, 32767.0)));
    }
    for (const double value : listAt(call, kAttacks)) {
        parsed.attacks.push_back(static_cast<std::uint8_t>(std::clamp(value, 0.0, 255.0)));
    }
    if (kCfgCharModelArgument < call.size()) {
        if (const std::optional<std::string_view> model = call[kCfgCharModelArgument].string()) {
            parsed.model = std::string(*model);
        }
    }
    if (kHat < call.size()) {
        if (const std::optional<std::string_view> hat = call[kHat].string()) {
            parsed.hat = std::string(hat->substr(0, kMaxHatName));
        }
    }
    return parsed;
}

combat::PowerClass parseCfgPowerClass(std::span<const script::Value> call, const combat::PowerClass& base) {
    combat::PowerClass power = base;
    const auto whole = [&call](std::size_t index, int& field) {
        if (const std::optional<int> value = wholeAt(call, index)) {
            field = *value;
        }
    };
    const auto real = [&call](std::size_t index, float& field) {
        if (const std::optional<double> value = numberAt(call, index)) {
            field = static_cast<float>(*value);
        }
    };
    whole(kPowerMax, power.powerMax);
    whole(kPowerRefill, power.refillPerSecond);
    whole(kStaminaMax, power.staminaMax);
    whole(kStaminaRefill, power.staminaRefillPerSecond);
    whole(kGroundMs, power.groundMs);
    whole(kStunMs, power.stunMs);
    whole(kStruggle, power.struggleDivisor);
    // The struggle divides by it: never 0.
    power.struggleDivisor = std::max(1, power.struggleDivisor);
    real(kHurt, power.hurtFraction);
    real(kBlock, power.blockChance);
    real(kHurtBlock, power.hurtBlockChance);
    real(kHurtPower, power.hurtPowerFactor);
    real(kDelayFactor, power.attackDelayFactor);
    real(kDelayDownFactor, power.attackDelayDownFactor);
    real(kCounter, power.counterChance);
    power.counterChance = std::clamp(power.counterChance, 0.0F, 1.0F);
    int walls = power.throwsAtWalls ? 1 : 0;
    whole(kThrowsAtWalls, walls);
    power.throwsAtWalls = std::clamp(walls, 0, 1) == 1;
    whole(kRingByte, power.ringByte);
    power.ringByte = std::clamp(power.ringByte, 0, kMaxRingByte);
    return power;
}

CharacterTypes CharacterTypes::fromRecorded(const script::RecordedCalls& recorded) {
    CharacterTypes types;
    // The power classes over the player's defaults, and the Warrior classes' damage scales; the last call wins.
    for (const std::vector<script::Value>& call : recorded.calls("CfgPowerClass")) {
        if (const std::optional<int> id = wholeAt(call, 0)) {
            putSorted(types.m_powerClasses, *id, parseCfgPowerClass(call, combat::kPlayerPowerClass));
        }
    }
    for (const std::vector<script::Value>& call : recorded.calls("CfgWarriorClass")) {
        if (const std::optional<int> id = wholeAt(call, kWarriorClassId)) {
            putSorted(types.m_warriorClasses, *id, parseCfgWarriorClass(call));
        }
    }
    for (const std::vector<script::Value>& call : recorded.calls("CfgChar")) {
        std::optional<CharacterType> parsed = parseCfgChar(call);
        if (!parsed) {
            continue;
        }
        // Kept sorted; a later call of a type replaces the earlier one.
        auto at = std::ranges::lower_bound(types.m_types, parsed->type, {}, &CharacterType::type);
        if (at != types.m_types.end() && at->type == parsed->type) {
            *at = std::move(*parsed);
        } else {
            types.m_types.insert(at, std::move(*parsed));
        }
    }
    return types;
}

const CharacterType* CharacterTypes::find(int type) const {
    const auto at = std::ranges::lower_bound(m_types, type, {}, &CharacterType::type);
    return at != m_types.end() && at->type == type ? &*at : nullptr;
}

std::optional<PlayerTraits> CharacterTypes::playerTraitsOf(int type) const {
    const CharacterType* own = find(type);
    if (own == nullptr) {
        return std::nullopt;
    }
    PlayerTraits traits;
    // The damage comes from the class's record; the category and power class from the type's own.
    traits.classType = characterClassOf(type).id;
    if (const CharacterType* classRecord = find(traits.classType)) {
        traits.damage = classRecord->damage;
    }
    traits.powerClassId = powerClassOf(type, own->category.value_or(0), own->powerClass.value_or(0));
    if (const combat::PowerClass* power = findSorted(m_powerClasses, traits.powerClassId)) {
        traits.powerClass = *power;
    }
    traits.warriorClass = warriorClassOf(type);
    if (const WarriorClass* warrior = findSorted(m_warriorClasses, traits.warriorClass)) {
        traits.damagePercent = warrior->damagePercent.value_or(0);
        traits.healthPercent = warrior->healthPercent.value_or(0);
        traits.rage = warrior->rage;
    }
    // Human_MakePlayer's difficulty scaling: the health times the Warrior class's percentage, rounded
    // (docs/references/warrior-classes.md). **Coney's reading**: the type's own `+0x116`, as an AI human of the type
    // gets it (the page does not say which record Human_Init reads it from; every Warriors alias has its class's).
    traits.health = own->health.value_or(0);
    if (traits.healthPercent > 0) {
        traits.health =
            static_cast<int>(std::lround(static_cast<double>(traits.health) * traits.healthPercent / 100.0));
    }
    return traits;
}

std::optional<std::string> CharacterTypes::modelFor(int type, int playerIndex, int levelNumber) const {
    return modelNameFor(type, playerIndex, levelNumber, [this](int recordType) -> std::optional<std::string> {
        const CharacterType* record = find(recordType);
        if (record == nullptr || record->model.empty()) {
            return std::nullopt;
        }
        return record->model;
    });
}

} // namespace coney::characters
