// SPDX-License-Identifier: GPL-3.0-or-later
// The objects' message handlers and the volume boxes (docs/research/scripting.md#message-handlers,
// docs/research/scripting.md#triggers): SetMsgHandler's slots, the arguments each message number is delivered with,
// and AddVolumeBox / RotateVolumeBox. Synthetic scripts: a native function records what it is called with.
#include "scripting/trigger_bindings.h"

#include <array>
#include <cstddef>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "gui/global_strings.h"
#include "scripting/binding_args.h"
#include "scripting/lua_value.h"
#include "scripting/lua_vm.h"
#include "scripting/message_handlers.h"
#include "scripting/script_bindings.h"
#include "scripting/script_system.h"
#include "warriors/game_state.h"
#include "world_objects/volume_boxes.h"

using coney::script::LuaVm;
using coney::script::MessageHandlers;
using coney::script::ScriptSystem;
using coney::script::Table;
using coney::script::Value;

namespace {

// A host that ignores every request: these bindings ask nothing of it.
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

// A script system with Coney's bindings over handlers and boxes, and a native `Record` that keeps its arguments.
struct Harness {
    coney::GameState state;
    coney::gui::GlobalStrings strings;
    QuietHost host;
    coney::script::RecordedCalls recorded;
    MessageHandlers messages;
    coney::world_objects::VolumeBoxes boxes;
    coney::script::BindingContext context{&state,  &strings, &host,   &recorded, nullptr,
                                          nullptr, nullptr,  nullptr, &messages, &boxes};
    ScriptSystem scripts;
    std::vector<std::vector<Value>> recordedArgs;

    Harness()
        : scripts(
              [](std::string_view) -> std::expected<std::vector<std::byte>, coney::Error> {
                  return coney::fail(coney::ErrorCode::NotFound, "no scripts in this test");
              },
              [this](ScriptSystem& system, LuaVm& vm) { coney::script::installBindings(system, vm, context); }, {}) {
        scripts.create();
        scripts.vm().registerFunction("Record", [this](std::span<const Value> args) -> coney::script::binding::Results {
            recordedArgs.emplace_back(args.begin(), args.end());
            return std::vector<Value>{};
        });
    }

    // The first result of the binding `name` with `args`; nil when there is none. REQUIREs success.
    Value first(std::string_view name, const std::vector<Value>& args = {}) {
        auto result = scripts.vm().call(scripts.vm().global(name), args);
        REQUIRE(result.has_value());
        return result && !result->empty() ? (*result)[0] : Value();
    }

    // The numbers of the last recorded call, nil as -1.
    std::vector<double> lastNumbers() const {
        std::vector<double> numbers;
        REQUIRE_FALSE(recordedArgs.empty());
        for (const Value& value : recordedArgs.back()) {
            numbers.push_back(value.number().value_or(-1.0));
        }
        return numbers;
    }
};

// A Lua string value.
Value str(const char* text) { return Value(std::string(text)); }

// A table {x, y, z}.
Value triple(double x, double y, double z) {
    auto table = std::make_shared<Table>();
    REQUIRE(table->set(Value(1.0), Value(x)).has_value());
    REQUIRE(table->set(Value(2.0), Value(y)).has_value());
    REQUIRE(table->set(Value(3.0), Value(z)).has_value());
    return Value(table);
}

} // namespace

TEST_CASE("SetMsgHandler keeps a callback per object and message, and nil removes it", "[trigger_bindings]") {
    Harness h;
    h.first("SetMsgHandler", {Value(40.7), Value(3.0), str("P1.FirstGlow")});
    CHECK(h.messages.handler(40, 3) == "P1.FirstGlow");
    CHECK(h.messages.handler(40, 4).empty());
    h.first("SetMsgHandlerEx", {Value(41.0), Value(0.0), str("Talk"), str("prompt"), Value()});
    CHECK(h.messages.handler(41, 0) == "Talk");
    h.first("SetMsgHandler", {Value(40.0), Value(3.0), Value()});
    CHECK(h.messages.handler(40, 3).empty());
    h.first("SetMsgHandler", {Value(40.0), Value(26.0), str("Out")}); // beyond the 26 slots
    CHECK(h.messages.size() == 1);
}

TEST_CASE("a message reaches its handler with the arguments its number takes", "[trigger_bindings]") {
    Harness h;
    for (const int message : {1, 3, 6, 8, 9, 0x12, 0x19, 0xb}) {
        h.messages.set(5, message, "Record");
    }
    // 3: (self, subject).
    CHECK_FALSE(h.messages.deliver(h.scripts, 5, 3, 100, 200, 7));
    CHECK(h.lastNumbers() == std::vector<double>{5, 100});
    // 1: (self, other, n).
    h.messages.deliver(h.scripts, 5, 1, 100, 200, 7);
    CHECK(h.lastNumbers() == std::vector<double>{5, 200, 7});
    // 6: (subject, n, other).
    h.messages.deliver(h.scripts, 5, 6, 100, 200, 7);
    CHECK(h.lastNumbers() == std::vector<double>{100, 7, 200});
    // 8, a flag's arrival: (flag, human).
    h.messages.deliver(h.scripts, 5, 8, 100, 0, 0);
    CHECK(h.lastNumbers() == std::vector<double>{5, 100});
    // 9 asks for a result: (n); a call that ran takes it.
    CHECK(h.messages.deliver(h.scripts, 5, 9, 100, 200, 7));
    CHECK(h.lastNumbers() == std::vector<double>{7});
    // 0x12, died: (subject, attacker).
    h.messages.deliver(h.scripts, 5, 0x12, 100, 200, 7);
    CHECK(h.lastNumbers() == std::vector<double>{100, 200});
    // 0x19: (self, other, n, flag), the flag 0 unless a car sends it (deliverFromCar()).
    h.messages.deliver(h.scripts, 5, 0x19, 100, 200, 7);
    CHECK(h.lastNumbers() == std::vector<double>{5, 200, 7, 0});
    // A number the table lists as (self, other).
    h.messages.deliver(h.scripts, 5, 0xb, 100, 200, 7);
    CHECK(h.lastNumbers() == std::vector<double>{5, 200});
    // No handler, no call.
    const std::size_t calls = h.recordedArgs.size();
    CHECK_FALSE(h.messages.deliver(h.scripts, 6, 3, 100, 0, 0));
    CHECK(h.recordedArgs.size() == calls);
}

TEST_CASE("AddVolumeBox makes a box with a handle from the world objects' counter; kind 1 is refused",
          "[trigger_bindings]") {
    Harness h;
    const double first =
        h.first("AddVolumeBox", {str("vMark01"), Value(0.0), triple(-284.1, 126.2, 0.2), triple(1.6, 1.6, 3)})
            .number()
            .value_or(0.0);
    const double second =
        h.first("AddVolumeBox", {str("vMark03"), Value(0.0), triple(0, 0, 0), triple(1, 1, 1), Value()})
            .number()
            .value_or(0.0);
    CHECK(first > 0.0);
    CHECK(second == first + 1.0);
    CHECK(h.first("AddVolumeBox", {str("vNo"), Value(1.0), triple(0, 0, 0), triple(1, 1, 1)}).number() == 0.0);
    REQUIRE(h.boxes.all().size() == 2);

    const coney::world_objects::VolumeBox& mark = *h.boxes.find(first);
    CHECK(mark.name == "vMark01");
    CHECK(mark.enabled);
    CHECK(coney::world_objects::VolumeBoxes::inside(mark, {-283.3F, 127.0F, 1.0F}));
    CHECK_FALSE(h.boxes.find(second)->enabled); // nil is false

    h.first("RotateVolumeBox", {Value(first), Value(0.0), Value(-1.0), Value(1.0), Value(0.0)});
    CHECK(h.boxes.find(first)->turn == std::array<float, 4>{0, -1, 1, 0});
}
