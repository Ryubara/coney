// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.h"
#include "debug/entity_inspect.h"

// The Entity inspector's edits as data (docs/guides/debug-menu.md#entity-inspector): what an edit can do to a human or
// object, the queue the page puts edits in, the target the play mode implements and the run-log line each applied edit
// leaves. An edit is never written when the page asks: like TunableRegistry (src/debug/tunables.h) the page queues it
// and the game applies the queue between two simulation steps (the input gate does, src/debug/input_gate.h), so a step
// sees the same state from start to end. Every applied or refused edit is a line of the run log
// (docs/guides/run-log.md), and appliedEdits() reads those lines back, so a bug report replays exactly what was done.
//
// Coney-only debug tooling: the original has no counterpart (docs/research/debug.md#not-present).

namespace coney::debug {

/// What an edit does. Each takes up to three numbers (editOpArity()); a flag is 0 or 1, a meter is a percentage of its
/// maximum.
enum class EditOp : std::uint8_t {
    Teleport,       ///< x y z: the feet go there, dropped onto the ground below when the ground is under them.
    Heading,        ///< degrees: the facing (0 faces +y, anticlockwise from above).
    DropToGround,   ///< Nothing: the feet go down to the ground below them.
    Health,         ///< percent of the maximum, above 0 up to 100 (Kill is the way to 0).
    Stamina,        ///< percent of the maximum.
    Power,          ///< percent of the maximum.
    Rage,           ///< percent of the maximum.
    Invincible,     ///< 1 or 0: the human cannot be hurt (human flag god, 0x10).
    Kill,           ///< Nothing: health to 0, which the human's own update turns into a death.
    Scale,          ///< The body scale: the mesh and, unless ScaleCollision is off, the collision.
    ScaleCollision, ///< 1 or 0: whether the collision follows the mesh scale (on by default) or stays as configured.
    AnimRate,       ///< The playback rate of every clip, 1 is as configured.
    SpeedScale,     ///< A multiplier on the human's gait speeds, 1 is as configured.
    Frozen,         ///< 1 or 0: the Entity inspector's freeze.
    Delete,         ///< Nothing: the game's own removal (the way HuDelete takes a human out).
    Model,          ///< modelId(): another model drawn in place; everything else about the human stays.
};

/// How many EditOp values there are; EditOp values are 0 to kEditOpCount - 1.
inline constexpr std::size_t kEditOpCount = 16;

/// The name of `op` as the run log writes it (`health`, `scale-collision`).
[[nodiscard]] std::string_view editOpName(EditOp op);
/// The op named `name`; nothing for another name.
[[nodiscard]] std::optional<EditOp> editOpFromName(std::string_view name);
/// How many numbers `op` takes (0 to 3).
[[nodiscard]] std::size_t editOpArity(EditOp op);

/// One edit: which entity, what to do and with what numbers (those past editOpArity() are 0).
struct EntityCommand {
    EntityId entity = kNoEntity;
    EditOp op = EditOp::Health;
    std::array<double, 3> values{};
    friend bool operator==(const EntityCommand&, const EntityCommand&) = default;
};

/// The number a Model edit names model `model` by: the CRC-32 of its lower-case name, as the Character List keys its
/// models (docs/research/characters.md#files), so a run log names the same model in every run and on every disc
/// layout. Exact in a double, as every 32-bit number is.
[[nodiscard]] std::uint32_t modelId(std::string_view model);
/// The model of `models` whose modelId() is `id`; nothing when none is, or `id` is no 32-bit whole number.
[[nodiscard]] std::optional<std::string> modelWithId(std::span<const std::string> models, double id);

/// The command as the run log writes it after `applied` or `refused`: `<entity> <op> <numbers>`, each number as its
/// shortest text that reads back exactly (`131073 teleport -12.5 0.1 1`); no numbers for an op that takes none.
[[nodiscard]] std::string commandText(const EntityCommand& command);
/// The command `text` spells (commandText()'s form); fails with ErrorCode::Invalid for an unknown op, a wrong number
/// of values or text that is not a number.
[[nodiscard]] std::expected<EntityCommand, Error> parseCommand(std::string_view text);

/// The edits a run log applied, in order: from each of `lines` that holds `entity edit: applied <command>` (anywhere in
/// the line, so a stamped log line works), the command. Refused edits and other lines are skipped.
[[nodiscard]] std::vector<EntityCommand> appliedEdits(std::span<const std::string> lines);

/// What the Entity inspector edits: the play mode implements it over its humans and objects
/// (PlayControls::entityEdits()). Called between simulation steps only, apart from the reads.
class EntityEditTarget {
  public:
    virtual ~EntityEditTarget() = default;

    /// Applies `command` now. Fails, changing nothing, with the reason in the message (the entity is gone, the op does
    /// not apply to it, deleting player 1 is not supported yet).
    virtual std::expected<void, Error> applyEdit(const EntityCommand& command) = 0;
    /// The numbers a reset of `op` writes for `entity`: the value it was configured with. Nothing when the entity is
    /// gone or `op` has no configured value (a teleport or a kill).
    [[nodiscard]] virtual std::optional<std::array<double, 3>> configuredValues(EntityId entity, EditOp op) const = 0;
    /// The first number of `op` on `entity` now (its health in percent, its scale, its model's modelId()); nothing
    /// when it is not known.
    [[nodiscard]] virtual std::optional<double> currentValue(EntityId entity, EditOp op) const = 0;
    /// The models a Model edit can give a human here, sorted; empty (the default) where it cannot.
    [[nodiscard]] virtual std::vector<std::string> models() const { return {}; }
};

/// The queue of edits waiting for the next step boundary, with the run log line of each applied edit and the rule that
/// in a network session only the host applies them.
class EntityEditor {
  public:
    /// Where the editor writes its lines (`entity edit: applied 7 health 40`); empty: nowhere. The debug session
    /// writes them to the run log.
    using Log = std::function<void(std::string_view)>;

    explicit EntityEditor(Log log = {}) : m_log(std::move(log)) {}

    /// Replaces where the lines go (the debug session's default is the run log); empty: nowhere.
    void setLog(Log log) { m_log = std::move(log); }
    /// Sets who may edit: queue() and applyPending() refuse while it returns false (a network client). Empty: always.
    void setAuthority(std::function<bool()> mayEdit) { m_mayEdit = std::move(mayEdit); }

    /// Queues `command` for the next applyPending(). Returns false, queueing nothing and writing a `refused` line,
    /// where this game may not edit.
    bool queue(const EntityCommand& command);
    /// Queues the reset of `op` on `entity`: an ordinary edit writing the target's configured values. False, queueing
    /// nothing, where the target has no configured value for it or this game may not edit.
    bool queueReset(EntityId entity, EditOp op, const EntityEditTarget& target);

    /// Applies every queued edit in order to `target`, writing `applied` or `refused` for each; returns how many
    /// applied. With no target (null: nothing is playing) or where this game may not edit, every one is dropped with a
    /// `refused` line. Call it between simulation steps.
    std::size_t applyPending(EntityEditTarget* target);
    /// Edits queued and not yet applied.
    [[nodiscard]] std::size_t pending() const { return m_pending.size(); }
    /// The newest queued edit of `op` on `entity`; nothing when none waits.
    [[nodiscard]] std::optional<EntityCommand> queuedCommand(EntityId entity, EditOp op) const;
    /// The first number of queuedCommand(); nothing when none waits.
    [[nodiscard]] std::optional<double> queuedValue(EntityId entity, EditOp op) const;
    /// The last lines written (applied and refused), oldest first, at most kJournalLines.
    [[nodiscard]] const std::vector<std::string>& journal() const { return m_journal; }
    /// How many lines journal() keeps.
    static constexpr std::size_t kJournalLines = 64;

  private:
    // Writes one line to the log and the journal.
    void write(std::string line);
    // `command` refused for `reason`, as the log writes it.
    void refuse(const EntityCommand& command, std::string_view reason);

    Log m_log;
    std::function<bool()> m_mayEdit;
    std::vector<EntityCommand> m_pending;
    std::vector<std::string> m_journal;
};

} // namespace coney::debug
