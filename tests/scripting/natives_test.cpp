// SPDX-License-Identifier: GPL-3.0-or-later
// The native list (buildNatives): every binding of Coney's table as a Native record with its status and the mods
// flag, installed in a fresh state exactly as registered; and the session's handles, shared by every state of a
// session (docs/guides/asset-and-script-architecture.md#script-parts, docs/guides/studio/lua-sdk.md#two-states).
#include "scripting/natives.h"

#include <cstddef>
#include <expected>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "gui/global_strings.h"
#include "scripting/lua_value.h"
#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"
#include "scripting/script_handles.h"
#include "scripting/script_system.h"
#include "warriors/created_humans.h"
#include "warriors/game_state.h"

using coney::Error;
using coney::script::BindingKind;
using coney::script::LuaVm;
using coney::script::Native;
using coney::script::NativeOrigin;
using coney::script::ScriptSystem;
using coney::script::Value;

namespace {

// A host that does nothing: these tests look at the list, not at what the bindings ask of the menus.
class QuietHost final : public coney::script::BindingHost {
  public:
    void showProfileManager(std::string_view /*onRumble*/, std::string_view /*onStartGame*/) override {}
    void showRumbleModeInterface(std::string_view /*onCancel*/, std::string_view /*onStart*/,
                                 double /*players*/) override {}
    void menuLoadLevel(std::string_view /*level*/) override {}
    void playMovie(std::string_view /*name*/) override {}
    void playMusic(std::string_view /*track*/) override {}
    void stopMusic() override {}
    void queueScreenEffect(int /*type*/, double /*seconds*/) override {}
};

// One session's binding context over a game state, strings, the humans the scripts make and the session's handles,
// and a script system (one state at a time) with Coney's bindings over it.
struct Session {
    coney::GameState state;
    coney::gui::GlobalStrings strings;
    QuietHost host;
    coney::script::RecordedCalls recorded;
    coney::CreatedHumans humans;
    coney::script::ScriptHandles handles;
    coney::script::BindingContext context{&state, &strings, &host, &recorded, &humans};

    Session() { context.handles = &handles; }

    // A script system over this session's context, reading no scripts.
    [[nodiscard]] std::unique_ptr<ScriptSystem> makeState() {
        auto scripts = std::make_unique<ScriptSystem>(
            [](std::string_view /*name*/) -> std::expected<std::vector<std::byte>, Error> {
                return coney::fail(coney::ErrorCode::NotFound, "no scripts in this test");
            },
            [this](ScriptSystem& system, LuaVm& vm) { coney::script::installBindings(system, vm, context); },
            ScriptSystem::Log{});
        scripts->create();
        return scripts;
    }
};

// Calls the global `name` of `scripts`' state with `args` and returns its first result; nil when it has none.
Value first(ScriptSystem& scripts, std::string_view name, const std::vector<Value>& args) {
    auto results = scripts.vm().call(scripts.vm().global(name), args);
    REQUIRE(results.has_value());
    return results && !results->empty() ? results->front() : Value();
}

// `HuCreate(name, 32, {x, y, z}, 0, "warr_sw", 0, 1)`: a human at a position; returns its handle.
double makeHuman(ScriptSystem& scripts, const std::string& name, double x) {
    auto position = std::make_shared<coney::script::Table>();
    REQUIRE(position->set(Value(1.0), Value(x)).has_value());
    REQUIRE(position->set(Value(2.0), Value(2.0)).has_value());
    REQUIRE(position->set(Value(3.0), Value(3.0)).has_value());
    return first(scripts, "HuCreate",
                 {Value(name), Value(32.0), Value(std::move(position)), Value(0.0), Value(std::string("warr_sw")),
                  Value(0.0), Value(1.0)})
        .number()
        .value_or(0.0);
}

} // namespace

TEST_CASE("the native list holds every binding of the table once, with its status", "[natives]") {
    // The table's kinds are the statuses.
    // Expected: scripting.md#coneys-implementation, confirmed (code)
    Session session;
    const std::unique_ptr<ScriptSystem> scripts = session.makeState();
    const std::vector<Native> natives = coney::script::buildNatives(*scripts, session.context);

    std::set<std::string, std::less<>> names;
    for (const Native& native : natives) {
        CHECK(names.insert(native.name).second); // once each
        CHECK(native.call);
        CHECK(native.origin == NativeOrigin::Original);
    }
    for (const coney::script::BindingInfo& info : coney::script::bindingTable()) {
        const Native* native = coney::script::findNative(natives, info.name);
        REQUIRE(native != nullptr);
        CHECK(native->status == info.kind);
    }
    // Nothing is registered that the table does not list.
    CHECK(natives.size() == coney::script::bindingTable().size());
    CHECK(coney::script::findNative(natives, "GetPlatform")->status == BindingKind::Real);
    CHECK(coney::script::findNative(natives, "PlayMovie")->status == BindingKind::Routed);
    CHECK(coney::script::findNative(natives, "GoalPathBlocker") == nullptr);
}

TEST_CASE("the native list says which natives a mod may call", "[natives]") {
    // The SDK design's first list of game natives that are not for mods (docs/guides/studio/lua-sdk.md#availability),
    // a Coney decision, not a fact about the original.
    // Expected: unverified, #1118
    Session session;
    const std::unique_ptr<ScriptSystem> scripts = session.makeState();
    const std::vector<Native> natives = coney::script::buildNatives(*scripts, session.context);
    for (const std::string_view name : {"doFile", "preLoadFile", "SetStartGameCallback"}) {
        const Native* native = coney::script::findNative(natives, name);
        REQUIRE(native != nullptr);
        CHECK(!native->mods);
    }
    CHECK(coney::script::findNative(natives, "HuCreate")->mods);
    CHECK(!coney::script::allowedForMods("SetHatCallBack"));
    CHECK(coney::script::allowedForMods("GetGameTime"));
}

TEST_CASE("the state installs the native list and keeps it for the debug menus", "[natives]") {
    // Coney's own seam: the state's globals are the list's natives.
    // Expected: unverified, #1118
    Session session;
    const std::unique_ptr<ScriptSystem> scripts = session.makeState();
    const std::span<const Native> installed = scripts->natives();
    REQUIRE(installed.size() == coney::script::bindingTable().size());
    for (const Native& native : installed) {
        CHECK(scripts->vm().global(native.name).function() != nullptr);
    }
    scripts->destroy();
    CHECK(scripts->natives().empty());
}

TEST_CASE("a name registered again keeps its place and takes the new call", "[natives]") {
    // The list keeps the order a VM's globals had when they were set one by one.
    // Expected: unverified, #1118
    coney::script::NativeList list;
    list.add("A", [](std::span<const Value>) -> std::expected<std::vector<Value>, Error> {
        return std::vector<Value>{Value(1.0)};
    });
    list.add("B",
             [](std::span<const Value>) -> std::expected<std::vector<Value>, Error> { return std::vector<Value>{}; });
    list.add("A", [](std::span<const Value>) -> std::expected<std::vector<Value>, Error> {
        return std::vector<Value>{Value(2.0)};
    });
    const std::vector<Native> natives = list.take();
    REQUIRE(natives.size() == 2);
    CHECK(natives[0].name == "A");
    CHECK(natives[1].name == "B");
    const auto results = natives[0].call({});
    REQUIRE(results.has_value());
    CHECK(results->front().number() == 2.0);
}

TEST_CASE("a handle made in one state resolves in another state of the same session", "[natives]") {
    // Handles come from the session (docs/guides/studio/lua-sdk.md#two-states).
    // Expected: unverified, #1118
    Session session;
    const std::unique_ptr<ScriptSystem> game = session.makeState();
    const std::unique_ptr<ScriptSystem> other = session.makeState();
    const double made = makeHuman(*game, "Cleon", 7.0);
    REQUIRE(made >= 1.0);
    // The other state names the same human by the same handle.
    const Value position = first(*other, "HuGetPosition", {Value(made)});
    REQUIRE(position.table() != nullptr);
    CHECK(position.table()->field("x").number() == 7.0);
    // And its own handles never repeat one the session gave.
    const double second = makeHuman(*other, "Vermin", 9.0);
    CHECK(second != made);
    CHECK(first(*game, "HuGetPosition", {Value(second)}).table()->field("x").number() == 9.0);
}

TEST_CASE("a state made again after an unload gives no handle an earlier state gave", "[natives]") {
    // One counter per session, not per Lua state.
    // Expected: unverified, #1118
    Session session;
    const std::unique_ptr<ScriptSystem> scripts = session.makeState();
    const double before = makeHuman(*scripts, "Ajax", 1.0);
    scripts->create();
    const double after = makeHuman(*scripts, "Snow", 1.0);
    CHECK(after > before);
}
