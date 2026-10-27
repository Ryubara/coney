// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/type_traits.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <span>
#include <vector>

#include "characters/character_types.h"
#include "human/fighter.h"
#include "scripting/lua_value.h"
#include "scripting/script_bindings.h"

namespace coney::audio {

namespace {

// @coney-only Coney's reading of the recorded configuration (see typeTraits).
// The type a recorded `CfgChar` call configures, read as characters::parseCfgChar() reads it (its first argument
// truncated to an int, clamped to the int range first), without parsing the rest of the call.
std::optional<int> recordType(std::span<const script::Value> call) {
    const std::optional<double> number = call.empty() ? std::nullopt : call.front().number();
    if (!number || std::isnan(*number)) {
        return std::nullopt;
    }
    return static_cast<int>(std::trunc(std::clamp(*number, -2147483648.0, 2147483647.0)));
}

} // namespace

// @coney-only Coney's lookup in the recorded configuration; the original reads the type's table in memory.
HumanTraits typeTraits(const script::RecordedCalls& recorded, int type) {
    HumanTraits traits;
    // The type's last CfgChar record: its women's voices and its class. Another type's record is passed by its first
    // argument alone, since parsing it copies its tables and names.
    const std::span<const std::vector<script::Value>> chars = recorded.calls("CfgChar");
    for (auto call = chars.rbegin(); call != chars.rend(); ++call) {
        if (recordType(*call) != type) {
            continue;
        }
        const std::optional<characters::CharacterType> parsed = characters::parseCfgChar(*call);
        if (parsed) {
            traits.female = parsed->female.value_or(0) == 1;
            traits.bossClass = parsed->category.value_or(0) == human::kBossCategory;
            break;
        }
    }
    // CfgBreathingSound's first argument is the combat factor (game state +0x24c).
    if (const std::span<const std::vector<script::Value>> breathing = recorded.calls("CfgBreathingSound");
        !breathing.empty() && !breathing.back().empty()) {
        if (const std::optional<double> factor = breathing.back().front().number(); factor) {
            traits.combatFactor = static_cast<float>(*factor);
        }
    }
    return traits;
}

} // namespace coney::audio
