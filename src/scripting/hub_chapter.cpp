// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripting/hub_chapter.h"

#include <array>
#include <expected>
#include <format>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/assert.h"
#include "core/error.h"
#include "scripting/lua_value.h"
#include "scripting/lua_vm.h"
#include "scripting/script_system.h"

namespace coney::script {

namespace {

// Chapter n's configuration a new chapter drops: its scenes' cast, its scenes, its side mission and its scene extras
// (the humans clubhouse.Setup makes from SceneExtrasTable). Its actions are replaced rather than dropped
// (defaultActions()).
constexpr std::array<std::string_view, 4> kDroppedConfiguration{"CfgMissionHumans", "CfgSceneTables", "CfgMissionTable",
                                                                "SceneExtrasTable"};

// Sets `table[key]` to `value`; a string key and a value read from the scripts can always be set.
// @coney-only the hub chapters that mod missions open
void setField(Table& table, std::string_view key, Value value) {
    const auto set = table.set(Value(std::string(key)), std::move(value));
    CONEY_ASSERT(set.has_value());
}

// `fRunMission[checkpoint]`, the chapter's row (its giver's flag and its mission's level); null when there is none.
// @coney-only the hub chapters that mod missions open
std::shared_ptr<Table> runMissionRow(const LuaVm& vm, int checkpoint) {
    const Value rows = vm.global("fRunMission");
    if (!rows.table()) {
        return nullptr;
    }
    return rows.table()->get(Value(static_cast<double>(checkpoint))).table();
}

// Coney's default chapter's `CfgMissionActions`: the actions become a red circle at the chapter's giver flag, then
// the load of `fRunMission[checkpoint].level` (the original's chapter 11 shape, level95_chapter11.lua:f0), and, as a
// chapter's own does, it removes itself once it has run, so a second clubhouse set-up keeps the actions it made.
// @coney-only the hub chapters that mod missions open
NativeFunction defaultActions(LuaVm& vm, int checkpoint) {
    return [&vm, checkpoint](std::span<const Value> /*args*/) -> std::expected<std::vector<Value>, Error> {
        auto circle = std::make_shared<Table>();
        setField(*circle, "actionType", vm.global("MA_REDCIRCLE"));
        if (const std::shared_ptr<Table> row = runMissionRow(vm, checkpoint)) {
            setField(*circle, "flag", row->field("flag"));
        }
        auto load = std::make_shared<Table>();
        setField(*load, "actionType", vm.global("MA_LOADLEVEL"));
        auto actions = std::make_shared<Table>();
        for (const auto& [index, action] : {std::pair{1.0, circle}, std::pair{2.0, load}}) {
            const auto set = actions->set(Value(index), Value(action));
            CONEY_ASSERT(set.has_value());
        }
        vm.setGlobal("MissionAction", Value(std::move(actions)));
        vm.setGlobal("CfgMissionActions", Value());
        return std::vector<Value>{};
    };
}

// Puts Coney's default chapter in place of chapter n's configuration: drops kDroppedConfiguration and installs
// defaultActions().
// @coney-only the hub chapters that mod missions open
void installDefaultChapter(LuaVm& vm, int checkpoint) {
    for (const std::string_view name : kDroppedConfiguration) {
        vm.setGlobal(name, Value());
    }
    vm.registerFunction("CfgMissionActions", defaultActions(vm, checkpoint));
}

} // namespace

void applyHubChapter(ScriptSystem& scripts, const story::HubChapterPlan& plan) {
    if (!scripts.exists()) {
        return;
    }
    LuaVm& vm = scripts.vm();
    const std::shared_ptr<Table> row = runMissionRow(vm, plan.checkpoint);
    if (!row) {
        return;
    }
    // The offer the chapter's last action loads. A mod's mission without a number (level 0, until the natives number
    // it: #1300) loads nothing, rather than the mission chapter n offered before.
    Value level;
    if (plan.offer && plan.offer->level > 0) {
        level = Value(static_cast<double>(plan.offer->level));
    }
    setField(*row, "level", std::move(level));
    if (plan.original) {
        return;
    }

    // A new chapter: once chapter n's file has run (Main asked for it with RunLevel as the callback), its actions are
    // replaced before RunLevel sets the clubhouse up; then RunLevel is the chapter's own again and runs.
    const Value chapterRunLevel = vm.global("RunLevel");
    vm.registerFunction(
        "RunLevel",
        [&scripts, chapterRunLevel, plan](std::span<const Value> args) -> std::expected<std::vector<Value>, Error> {
            LuaVm& machine = scripts.vm();
            installDefaultChapter(machine, plan.checkpoint);
            // The mod's chapter script runs over the default and may set any of it itself. A script that fails
            // (runFile() logs and counts its error) may have replaced part of the default before it stopped, so the
            // default's own functions and tables go back in place (other globals it set stay), and the log says so.
            if (!plan.script.empty() && !scripts.runFile(plan.script)) {
                installDefaultChapter(machine, plan.checkpoint);
                scripts.log(std::format("hub chapter {}: its chapter script {} failed; Coney's default chapter stays",
                                        plan.id, plan.script));
            }
            machine.setGlobal("RunLevel", chapterRunLevel);
            scripts.call("RunLevel", args);
            return std::vector<Value>{};
        });
}

} // namespace coney::script
