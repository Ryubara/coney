// SPDX-License-Identifier: GPL-3.0-or-later
// The Entity editor's queue (docs/guides/debug-menu.md#entity-inspector): an edit is queued and lands only when the
// game applies the queue between two steps, every applied edit is written to the run log, a recorded log replays to the
// same state, only the host applies edits in a network session, a reset queues the configured value and a model edit
// names its model by the hash of its name. A fake target stands in for the play mode; no game, no disc.
#include "debug/entity_edit.h"

#include <array>
#include <cstddef>
#include <expected>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/log.h"
#include "core/name_hash.h"

using coney::debug::EditOp;
using coney::debug::EntityCommand;
using coney::debug::EntityEditor;
using coney::debug::EntityEditTarget;
using coney::debug::EntityId;

namespace {

// One fake entity's state: the numbers each op writes.
struct FakeState {
    double health = 100.0;
    double scale = 1.0;
    bool invincible = false;
    std::array<double, 3> feet{};
    bool deleted = false;
    friend bool operator==(const FakeState&, const FakeState&) = default;
};

// Entities 7 and 8, whose scale is configured at 1.0 and 1.5; 99 is player 1, who cannot be deleted. It also counts the
// applies, so a test sees when a queued edit lands.
class FakeTarget final : public EntityEditTarget {
  public:
    FakeTarget() {
        states[7] = FakeState{};
        states[8] = FakeState{.scale = 1.5};
        states[99] = FakeState{};
    }
    std::expected<void, coney::Error> applyEdit(const EntityCommand& command) override {
        ++applies;
        const auto found = states.find(command.entity);
        if (found == states.end() || found->second.deleted) {
            return coney::fail(coney::ErrorCode::NotFound, "no such entity");
        }
        FakeState& state = found->second;
        switch (command.op) {
        case EditOp::Health:
            state.health = command.values[0];
            return {};
        case EditOp::Scale:
            state.scale = command.values[0];
            return {};
        case EditOp::Invincible:
            state.invincible = command.values[0] != 0.0;
            return {};
        case EditOp::Teleport:
            state.feet = command.values;
            return {};
        case EditOp::Delete:
            if (command.entity == 99) {
                return coney::fail(coney::ErrorCode::InvalidArgument, "player 1 cannot be deleted yet");
            }
            state.deleted = true;
            return {};
        default:
            return coney::fail(coney::ErrorCode::InvalidArgument, "not supported here");
        }
    }
    std::optional<std::array<double, 3>> configuredValues(EntityId entity, EditOp op) const override {
        const auto found = states.find(entity);
        if (found == states.end()) {
            return std::nullopt;
        }
        switch (op) {
        case EditOp::Health:
            return std::array<double, 3>{100.0, 0.0, 0.0};
        case EditOp::Scale:
            return std::array<double, 3>{entity == 8 ? 1.5 : 1.0, 0.0, 0.0};
        case EditOp::Invincible:
            return std::array<double, 3>{0.0, 0.0, 0.0};
        default:
            return std::nullopt;
        }
    }
    std::optional<double> currentValue(EntityId entity, EditOp op) const override {
        const auto found = states.find(entity);
        if (found == states.end()) {
            return std::nullopt;
        }
        switch (op) {
        case EditOp::Health:
            return found->second.health;
        case EditOp::Scale:
            return found->second.scale;
        default:
            return std::nullopt;
        }
    }
    std::map<EntityId, FakeState> states;
    int applies = 0;
};

// An editor whose log lines are kept in `lines`.
struct Harness {
    std::vector<std::string> lines;
    EntityEditor editor{[this](std::string_view line) { lines.emplace_back(line); }};
};

} // namespace

TEST_CASE("an edit is queued and lands only when the queue is applied between steps", "[debug][entity-editor]") {
    // Expected: unverified, #723 (Coney's own debug tooling: the original has no entity editor)
    Harness h;
    FakeTarget target;
    REQUIRE(h.editor.queue(EntityCommand{.entity = 7, .op = EditOp::Health, .values = {40.0, 0.0, 0.0}}));
    REQUIRE(h.editor.queue(EntityCommand{.entity = 8, .op = EditOp::Scale, .values = {2.0, 0.0, 0.0}}));
    CHECK(h.editor.pending() == 2);
    // Nothing is written while the queue waits: a step sees the same state from start to end.
    CHECK(target.states[7].health == 100.0);
    CHECK(target.states[8].scale == 1.5);
    CHECK(target.applies == 0);

    CHECK(h.editor.applyPending(&target) == 2);
    CHECK(h.editor.pending() == 0);
    CHECK(target.states[7].health == 40.0);
    CHECK(target.states[8].scale == 2.0);
    // A second apply with nothing queued does nothing.
    CHECK(h.editor.applyPending(&target) == 0);
    CHECK(target.applies == 2);
}

TEST_CASE("edits apply in the order they were queued, the last write to a field wins", "[debug][entity-editor]") {
    // Expected: unverified, #723
    Harness h;
    FakeTarget target;
    h.editor.queue(EntityCommand{.entity = 7, .op = EditOp::Health, .values = {10.0, 0.0, 0.0}});
    h.editor.queue(EntityCommand{.entity = 7, .op = EditOp::Health, .values = {55.0, 0.0, 0.0}});
    h.editor.queue(EntityCommand{.entity = 7, .op = EditOp::Teleport, .values = {1.0, 2.0, 3.0}});
    h.editor.applyPending(&target);
    CHECK(target.states[7].health == 55.0);
    CHECK(target.states[7].feet == std::array<double, 3>{1.0, 2.0, 3.0});
}

TEST_CASE("each applied edit is written to the run log, and a refused one says why", "[debug][entity-editor]") {
    // Expected: unverified, #723 (docs/guides/run-log.md: the engine category takes any other prefix)
    Harness h;
    FakeTarget target;
    h.editor.queue(EntityCommand{.entity = 7, .op = EditOp::Health, .values = {40.0, 0.0, 0.0}});
    h.editor.queue(EntityCommand{.entity = 99, .op = EditOp::Delete});
    h.editor.queue(EntityCommand{.entity = 5, .op = EditOp::Scale, .values = {2.0, 0.0, 0.0}});
    CHECK(h.editor.applyPending(&target) == 1);
    REQUIRE(h.lines.size() == 3);
    CHECK(h.lines[0] == "entity edit: applied 7 health 40");
    CHECK(h.lines[1] == "entity edit: refused 99 delete: player 1 cannot be deleted yet");
    CHECK(h.lines[2] == "entity edit: refused 5 scale 2: no such entity");
    // The refused edits changed nothing.
    CHECK_FALSE(target.states[99].deleted);
}

TEST_CASE("the edits reach the run log's ring as engine lines", "[debug][entity-editor]") {
    // Expected: unverified, #723
    coney::logging::reset();
    EntityEditor editor{[](std::string_view line) { coney::logging::info(coney::logging::Category::Engine, line); }};
    FakeTarget target;
    editor.queue(EntityCommand{.entity = 7, .op = EditOp::Invincible, .values = {1.0, 0.0, 0.0}});
    editor.applyPending(&target);
    bool found = false;
    for (const coney::logging::Record& record : coney::logging::ring().records()) {
        found = found || record.text == "entity edit: applied 7 invincible 1";
    }
    CHECK(found);
    coney::logging::reset();
}

TEST_CASE("a command's text round-trips exactly, including fractions", "[debug][entity-editor]") {
    // Expected: unverified, #723
    const EntityCommand command{.entity = 131073, .op = EditOp::Teleport, .values = {-12.345678901234, 0.1, 1e-7}};
    const std::string text = coney::debug::commandText(command);
    CHECK(text == "131073 teleport -12.345678901234 0.1 1e-07");
    const auto parsed = coney::debug::parseCommand(text);
    REQUIRE(parsed.has_value());
    CHECK(*parsed == command);
    // A command with no values prints none.
    CHECK(coney::debug::commandText(EntityCommand{.entity = 7, .op = EditOp::Kill}) == "7 kill");
    // Bad text is refused, not guessed at.
    CHECK_FALSE(coney::debug::parseCommand("7 nonsense 1").has_value());
    CHECK_FALSE(coney::debug::parseCommand("seven health 1").has_value());
    CHECK_FALSE(coney::debug::parseCommand("7 health").has_value());
    CHECK_FALSE(coney::debug::parseCommand("7 health 1 2").has_value());
    CHECK_FALSE(coney::debug::parseCommand("").has_value());
}

TEST_CASE("a recorded run log replays to the same state", "[debug][entity-editor]") {
    // Expected: unverified, #723
    // The run: some edits applied, one refused, one more later.
    Harness first;
    FakeTarget before;
    first.editor.queue(EntityCommand{.entity = 7, .op = EditOp::Health, .values = {33.0, 0.0, 0.0}});
    first.editor.queue(EntityCommand{.entity = 8, .op = EditOp::Scale, .values = {0.25, 0.0, 0.0}});
    first.editor.queue(EntityCommand{.entity = 99, .op = EditOp::Delete});
    first.editor.applyPending(&before);
    first.editor.queue(EntityCommand{.entity = 7, .op = EditOp::Teleport, .values = {4.5, -2.25, 0.5}});
    first.editor.queue(EntityCommand{.entity = 8, .op = EditOp::Delete});
    first.editor.applyPending(&before);

    // Replay the log's lines (surrounded by other log text) into a fresh world.
    std::vector<std::string> log{"combat: knock-down: human 1 by human 2"};
    log.insert(log.end(), first.lines.begin(), first.lines.end());
    log.emplace_back("world: checkpoint 3");
    const std::vector<EntityCommand> replay = coney::debug::appliedEdits(log);
    REQUIRE(replay.size() == 4);

    Harness second;
    FakeTarget after;
    for (const EntityCommand& command : replay) {
        second.editor.queue(command);
    }
    second.editor.applyPending(&after);
    CHECK(after.states == before.states);
    // And the replay writes the same lines the run did (without the edit the first run refused).
    std::vector<std::string> applied;
    for (const std::string& line : first.lines) {
        if (line.starts_with("entity edit: applied")) {
            applied.push_back(line);
        }
    }
    CHECK(second.lines == applied);
}

TEST_CASE("only the host applies edits in a network session", "[debug][entity-editor]") {
    // Expected: unverified, #723 (docs/guides/network-architecture.md#checklist: state owned by the host)
    Harness h;
    FakeTarget target;
    bool host = false;
    h.editor.setAuthority([&host] { return host; });
    CHECK_FALSE(h.editor.queue(EntityCommand{.entity = 7, .op = EditOp::Health, .values = {1.0, 0.0, 0.0}}));
    CHECK(h.editor.pending() == 0);
    REQUIRE(h.lines.size() == 1);
    CHECK(h.lines[0] == "entity edit: refused 7 health 1: only the host edits in a network session");
    host = true;
    CHECK(h.editor.queue(EntityCommand{.entity = 7, .op = EditOp::Health, .values = {1.0, 0.0, 0.0}}));
    // The role lost before the queue is applied drops the queued edit as well.
    host = false;
    CHECK(h.editor.applyPending(&target) == 0);
    CHECK(target.states[7].health == 100.0);
}

TEST_CASE("with no play mode the queued edits are dropped with a line", "[debug][entity-editor]") {
    // Expected: unverified, #723
    Harness h;
    h.editor.queue(EntityCommand{.entity = 7, .op = EditOp::Health, .values = {1.0, 0.0, 0.0}});
    CHECK(h.editor.applyPending(nullptr) == 0);
    CHECK(h.editor.pending() == 0);
    REQUIRE(h.lines.size() == 1);
    CHECK(h.lines[0] == "entity edit: refused 7 health 1: nothing is playing");
}

TEST_CASE("a reset queues the value the entity is configured with", "[debug][entity-editor]") {
    // Expected: unverified, #723
    Harness h;
    FakeTarget target;
    h.editor.queue(EntityCommand{.entity = 8, .op = EditOp::Scale, .values = {4.0, 0.0, 0.0}});
    h.editor.applyPending(&target);
    REQUIRE(target.states[8].scale == 4.0);

    CHECK(h.editor.queueReset(8, EditOp::Scale, target));
    CHECK(h.editor.pending() == 1);
    h.editor.applyPending(&target);
    // Entity 8 is configured at 1.5, not at 1.
    CHECK(target.states[8].scale == 1.5);
    // The reset is an ordinary edit in the log, so it replays.
    CHECK(h.lines.back() == "entity edit: applied 8 scale 1.5");

    // An op with no configured value cannot be reset, and nothing is queued.
    CHECK_FALSE(h.editor.queueReset(8, EditOp::Teleport, target));
    CHECK_FALSE(h.editor.queueReset(1234, EditOp::Scale, target));
    CHECK(h.editor.pending() == 0);
}

TEST_CASE("the editor shows the queued value before it lands, and the current one after", "[debug][entity-editor]") {
    // Expected: unverified, #723
    Harness h;
    FakeTarget target;
    CHECK_FALSE(h.editor.queuedValue(7, EditOp::Health).has_value());
    h.editor.queue(EntityCommand{.entity = 7, .op = EditOp::Health, .values = {12.0, 0.0, 0.0}});
    CHECK(h.editor.queuedValue(7, EditOp::Health) == 12.0);
    CHECK_FALSE(h.editor.queuedValue(8, EditOp::Health).has_value());
    h.editor.applyPending(&target);
    CHECK_FALSE(h.editor.queuedValue(7, EditOp::Health).has_value());
}

TEST_CASE("every op has a name that parses back and a number of values", "[debug][entity-editor]") {
    // Expected: unverified, #723
    for (std::size_t i = 0; i < coney::debug::kEditOpCount; ++i) {
        const auto op = static_cast<EditOp>(i);
        const std::string_view name = coney::debug::editOpName(op);
        INFO(name);
        CHECK_FALSE(name.empty());
        CHECK(coney::debug::editOpFromName(name) == op);
        CHECK(coney::debug::editOpArity(op) <= 3);
    }
    CHECK(coney::debug::editOpArity(EditOp::Teleport) == 3);
    CHECK(coney::debug::editOpArity(EditOp::Kill) == 0);
    CHECK_FALSE(coney::debug::editOpFromName("nope").has_value());
}

TEST_CASE("a model edit names the model by the hash of its name, which reads back to the name",
          "[debug][entity-editor]") {
    // Expected: docs/guides/debug-menu.md#model-swap (the id is the Character List's name hash)
    const std::vector<std::string> models{"warr_cl", "warr_re_cv", "civ_ma_a"};
    // The id is the lower-case name's CRC-32, so a log line names the same model in every run.
    CHECK(coney::debug::modelId("warr_cl") == coney::nameHash("warr_cl"));
    CHECK(coney::debug::modelId("WARR_CL") == coney::debug::modelId("warr_cl"));
    CHECK(coney::debug::modelWithId(models, coney::debug::modelId("warr_re_cv")) ==
          std::optional<std::string>("warr_re_cv"));
    CHECK_FALSE(coney::debug::modelWithId(models, coney::debug::modelId("nobody")).has_value());
    CHECK_FALSE(coney::debug::modelWithId(models, 0.5).has_value());

    // The command takes the id as its one number and round-trips through its text exactly.
    const EntityCommand command{
        .entity = 7, .op = EditOp::Model, .values = {static_cast<double>(coney::debug::modelId("warr_cl")), 0.0, 0.0}};
    CHECK(coney::debug::editOpName(EditOp::Model) == "model");
    CHECK(coney::debug::editOpArity(EditOp::Model) == 1);
    const auto parsed = coney::debug::parseCommand(coney::debug::commandText(command));
    REQUIRE(parsed.has_value());
    CHECK(*parsed == command);
}
