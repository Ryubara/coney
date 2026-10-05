// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Synthetic Rumble menu chunks (docs/research/frontend.md#rumble-data), written like the disc's: each calls its
// `CfgRumble*` binding once per entry. The titles, names and descriptions are invented, never the game's; the ids,
// types and level numbers are the reference values the page lists, so the unlocks and the stand-in table apply to
// them as on the disc (LEGAL.md, "No game data").

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "support/lua_fixtures.h"

namespace coney::test {

// Pushes a table of `values` as the global `name`, then pushes the global again for a call.
inline void pushNumberTable(LuaAsm& code, const std::string& name, const std::vector<std::int32_t>& values) {
    code.newTable().setGlobal(name);
    for (std::size_t i = 0; i < values.size(); ++i) {
        code.getGlobal(name).pushInt(static_cast<std::int32_t>(i + 1)).pushInt(values[i]).setTable();
    }
    code.getGlobal(name);
}

/// One synthetic `CfgRumbleGame` call.
struct TestMode {
    std::string title;
    std::int32_t mode;
    bool onePlayer, coop, versus;
    std::int32_t size;
    std::int32_t preset1 = 0, preset2 = 0;
};

/// The synthetic modes: a duel (12: one player or versus, one a side), a squad fight (14: all three, five a side),
/// a locked endless fight (9) and a locked preset race (24, types 458 and 459). A fresh profile shows the first two.
inline std::vector<TestMode> testModes() {
    return {
        TestMode{"DUEL", 12, true, false, true, 1},
        TestMode{"SQUAD", 14, true, true, true, 5},
        TestMode{"ENDLESS", 9, true, false, true, 1},
        TestMode{"RACE", 24, true, false, true, 1, 458, 459},
    };
}

/// `rumble_data.lua`: one `CfgRumbleGame` per mode, each described as "about <title>".
inline std::vector<std::byte> rumbleModeChunk(const std::vector<TestMode>& modes = testModes()) {
    LuaAsm code;
    for (const TestMode& mode : modes) {
        code.getGlobal("CfgRumbleGame").pushString(mode.title).pushInt(mode.mode);
        for (const bool flag : {mode.onePlayer, mode.coop, mode.versus}) {
            if (flag) {
                code.pushInt(1);
            } else {
                code.pushNil();
            }
        }
        code.pushInt(mode.size).pushInt(mode.size).pushInt(mode.preset1).pushInt(mode.preset2);
        code.pushString("about " + mode.title).call(10);
    }
    return luaChunk(code.end());
}

/// One synthetic `CfgRumbleGang` call.
struct TestGang {
    std::int32_t id;
    std::string name;
    std::vector<std::int32_t> members;
};

/// The synthetic gangs, in list order: gang 5 with the roster whose first two types the stand-in table replaces
/// (89 and 90 become 91 and 94), a locked gang 13, and gang 3 (220 and 223 become 225 and 226). A fresh profile shows
/// gangs 5 and 3.
inline std::vector<TestGang> testGangs() {
    return {
        TestGang{5, "RED SIDE", {89, 90, 91, 92, 93, 94, 91, 92, 93}},
        TestGang{13, "LOCKED SIDE", {128, 129, 134, 130, 131, 132, 133, 313, 314}},
        TestGang{3, "BLUE SIDE", {220, 223, 224, 225, 226, 227, 228, 225, 226}},
    };
}

/// `rumble_gang.lua`: one `CfgRumbleGang` per gang.
inline std::vector<std::byte> rumbleGangChunk(const std::vector<TestGang>& gangs = testGangs()) {
    LuaAsm code;
    for (const TestGang& gang : gangs) {
        code.getGlobal("CfgRumbleGang").pushInt(gang.id).pushString(gang.name);
        pushNumberTable(code, "members", gang.members);
        code.call(3);
    }
    return luaChunk(code.end());
}

/// `rumble_arena.lua`: arena 101 (locked; mode 2 only), 102 (modes 12, 14 and 1) and 103 (every mode, locked).
inline std::vector<std::byte> rumbleArenaChunk() {
    LuaAsm code;
    const std::vector<std::pair<std::int32_t, std::vector<std::int32_t>>> arenas{
        {101, {2, -1}}, {102, {12, 14, 1, -1}}, {103, {0}}};
    for (const auto& [level, modes] : arenas) {
        code.getGlobal("CfgRumbleArena").pushInt(level).pushInt(9);
        pushNumberTable(code, "modes", modes);
        code.call(3);
    }
    return luaChunk(code.end());
}

/// `config_preload3.lua`'s level table for the arenas: record 0 `level100`, then `level101` to `level103` at indices
/// 42 to 44, each with an invented title (`ARENA 101` ...) as its fifth argument.
inline std::vector<std::byte> rumbleLevelTableChunk() {
    LuaAsm code;
    code.getGlobal("CfgLevelName").pushInt(0).pushString("level100").pushString("").pushString("level100");
    code.pushString("").pushInt(100).call(6);
    for (std::int32_t level = 101; level <= 103; ++level) {
        const std::string name = "level" + std::to_string(level);
        code.getGlobal("CfgLevelName").pushInt(level - 59).pushString(name).pushString("").pushString(name);
        code.pushString("ARENA " + std::to_string(level)).pushInt(level).call(6);
    }
    return luaChunk(code.end());
}

/// The three Rumble chunks by their WAD names, to add to a test's script files.
inline void addRumbleChunks(std::map<std::string, std::vector<std::byte>, std::less<>>& files) {
    files["rumble_data.lua"] = rumbleModeChunk();
    files["rumble_gang.lua"] = rumbleGangChunk();
    files["rumble_arena.lua"] = rumbleArenaChunk();
}

} // namespace coney::test
