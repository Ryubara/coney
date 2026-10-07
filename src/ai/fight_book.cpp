// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/fight_book.h"

#include <optional>
#include <span>
#include <vector>

#include "scripting/lua_value.h"
#include "scripting/script_bindings.h"

namespace coney::ai {

namespace {

// Argument `index` of `call` truncated to an integer, else 0.
int intAt(std::span<const script::Value> call, std::size_t index) {
    const std::optional<double> value = index < call.size() ? call[index].number() : std::nullopt;
    return value.has_value() ? static_cast<int>(*value) : 0;
}

} // namespace

GangFightTable gangFightTableFrom(const script::RecordedCalls& recorded) {
    GangFightTable table{};
    for (const std::vector<script::Value>& call : recorded.calls("CfgGang")) {
        const int kind = intAt(call, 0);
        if (kind < 0 || kind >= static_cast<int>(kGangKinds)) {
            continue;
        }
        // CfgGang(kind, v2, v3, v4, v5, strategy, v7, v8, v9): the later call wins.
        GangFightValues& values = table[static_cast<std::size_t>(kind)];
        values.standingSpacing = intAt(call, 1);
        values.downSpacing = intAt(call, 2);
        values.handOver = intAt(call, 4);
        values.tackle = intAt(call, 6);
        values.rearGrab = intAt(call, 7);
    }
    return table;
}

} // namespace coney::ai
