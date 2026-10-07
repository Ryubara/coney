// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/ai_config.h"

#include <algorithm>
#include <optional>
#include <span>

#include "characters/character_types.h"
#include "scripting/lua_value.h"

namespace coney::ai {

namespace {

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

// The fighter class from type `type`'s `CfgChar` call (characters::parseCfgChar()).
void readCharacter(std::span<const script::Value> call, AiConfig& config) {
    const std::optional<characters::CharacterType> parsed = characters::parseCfgChar(call);
    if (!parsed) {
        return;
    }
    ++config.callsRead;
    if (parsed->behaviour) {
        config.fighter.brain = brainTypeOf(*parsed->behaviour);
    }
    if (parsed->health.has_value() && *parsed->health >= 1) {
        config.fighter.health = *parsed->health;
    }
    if (parsed->attacks.size() >= kAttackKinds) {
        std::copy_n(parsed->attacks.begin(), kAttackKinds, config.settings.attackWeights.begin());
    }
    config.fighter.damage = parsed->damage;
}

// The power class's fields from its `CfgPowerClass` call (characters::parseCfgPowerClass()).
void readPowerClass(std::span<const script::Value> call, AiConfig& config) {
    if (call.empty()) {
        return;
    }
    ++config.callsRead;
    config.powerClass = characters::parseCfgPowerClass(call, config.powerClass);
}

// Argument `index` of `call` truncated to an integer, else `fallback`.
int intAt(std::span<const script::Value> call, std::size_t index, int fallback) {
    const std::optional<double> value = numberAt(call, index);
    return value.has_value() ? static_cast<int>(*value) : fallback;
}

// The enemy score's weights from the last `CfgSetTargetingPoints` and `CfgSetTargetingPointsEx` calls (each argument
// that is a number replaces its weight; CfgSetTargetingPoints' sixth is read and dropped by the original).
void readTargeting(const script::RecordedCalls& recorded, AiConfig& config) {
    TargetingPoints& points = config.settings.targeting;
    const std::span<const std::vector<script::Value>> plain = recorded.calls("CfgSetTargetingPoints");
    if (!plain.empty()) {
        const std::span<const script::Value> call = plain.back();
        points.perMetre = static_cast<float>(numberAt(call, 0).value_or(points.perMetre));
        points.stunned = intAt(call, 1, points.stunned);
        points.outOfView = intAt(call, 2, points.outOfView);
        points.down = intAt(call, 3, points.down);
        points.grabbed = intAt(call, 4, points.grabbed);
        points.nearTrain = intAt(call, 6, points.nearTrain);
        points.targetsMe = intAt(call, 7, points.targetsMe);
        points.previousTarget = intAt(call, 8, points.previousTarget);
        ++config.callsRead;
    }
    const std::span<const std::vector<script::Value>> extra = recorded.calls("CfgSetTargetingPointsEx");
    if (!extra.empty()) {
        const std::span<const script::Value> call = extra.back();
        points.leaderPerMetre = static_cast<float>(numberAt(call, 0).value_or(points.leaderPerMetre));
        points.enemyLeads = intAt(call, 1, points.enemyLeads);
        points.grabbedFromRear = intAt(call, 2, points.grabbedFromRear);
        points.running = intAt(call, 3, points.running);
        points.tagging = intAt(call, 4, points.tagging);
        points.targetsMeArmed = intAt(call, 5, points.targetsMeArmed);
        points.noWalkableLine = intAt(call, 6, points.noWalkableLine);
        points.cannotChase = intAt(call, 7, points.cannotChase);
        points.player = intAt(call, 8, points.player);
        ++config.callsRead;
    }
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
    readTargeting(recorded, config);
    return config;
}

} // namespace coney::ai
