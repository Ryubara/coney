// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripting/story_save.h"

#include <array>

#include "scripting/lua_value.h"
#include "scripting/script_system.h"

namespace coney::script {

// global.lua's main chunk first, for the records UM_Unlock matches, then each level's completion record.
void completeLevels(ScriptSystem& scripts, std::span<const int> levels) {
    if (!scripts.exists()) {
        return;
    }
    (void)scripts.runFile(kGlobalScript);
    for (const int level : levels) {
        const std::array<Value, 3> unlock{Value(static_cast<double>(level)), Value(0.0), Value(0.0)};
        (void)scripts.call("UM_Unlock", unlock);
    }
}

} // namespace coney::script
