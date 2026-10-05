// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "core/error.h"

namespace coney::debug {

/// What a tunable holds.
enum class TunableType : std::uint8_t {
    Bool,  ///< On or off.
    Int,   ///< A whole number.
    Float, ///< A real number.
};

/// One named, typed value a subsystem lets the debug menus edit while the game runs: a dead zone, a run speed, a
/// camera distance. The subsystem owns the variable; the registry holds a pointer to it and the metadata the menus
/// show. Values are kept as doubles in the registry's interface and converted to the variable's type when written.
///
/// Built with a chain on TunableRegistry::add(), so a registration is one line:
/// `registry.add("Movement", "Run speed", &m_runSpeed).range(0, 20, 0.1).units("m/s").describe("...")`.
///
/// Coney's own tool: nothing in the original corresponds (docs/research/debug.md#not-present).
class Tunable {
  public:
    /// The live variable this tunable edits.
    using Target = std::variant<bool*, int*, float*>;

    /// Sets the range and the step the menus change the value by: `step` per press, step / 10 with the fine modifier
    /// and step × 10 with the coarse one. `min` must not be above `max` and `step` must be positive (CONEY_ASSERT).
    Tunable& range(double min, double max, double step);
    /// Sets the units shown after the value (`m/s`, `deg`); empty for none.
    Tunable& units(std::string units);
    /// Sets the one-line description the menus show as help.
    Tunable& describe(std::string description);

    /// The category, which is the menus' page for it (`Movement`).
    [[nodiscard]] const std::string& category() const { return m_category; }
    /// The name inside the category (`Run speed`).
    [[nodiscard]] const std::string& name() const { return m_name; }
    /// `category/name`, the key of the overrides file and of pins.
    [[nodiscard]] std::string path() const { return m_category + "/" + m_name; }
    /// The value's type.
    [[nodiscard]] TunableType type() const;
    /// The lowest value; 0 for a bool.
    [[nodiscard]] double min() const { return m_min; }
    /// The highest value; 1 for a bool.
    [[nodiscard]] double max() const { return m_max; }
    /// The normal step.
    [[nodiscard]] double step() const { return m_step; }
    /// The value the variable had when it was registered.
    [[nodiscard]] double defaultValue() const { return m_default; }
    /// The units; may be empty.
    [[nodiscard]] const std::string& unitText() const { return m_units; }
    /// The description; may be empty.
    [[nodiscard]] const std::string& description() const { return m_description; }

    /// The variable's value now.
    [[nodiscard]] double current() const;
    /// `value` clamped to the range and, for an int or a bool, rounded to a whole number.
    [[nodiscard]] double clamp(double value) const;
    /// The value as the menus show it: `on`/`off`, a whole number, or a real number with up to 3 decimals, then the
    /// units.
    [[nodiscard]] std::string format(double value) const;

  private:
    friend class TunableRegistry;
    Tunable(std::string category, std::string name, Target target);
    // Writes `value` (clamped) into the variable.
    void write(double value);

    std::string m_category;
    std::string m_name;
    Target m_target;
    double m_min = 0.0;
    double m_max = 1.0;
    double m_step = 1.0;
    double m_default = 0.0;
    std::string m_units;
    std::string m_description;
};

/// Every tunable, by category, with the changes waiting to be applied and the overrides saved to a file.
///
/// **Determinism.** set() never writes the variable: it queues the change, and applyPending() writes every queued
/// change at once. The game calls applyPending() between two simulation steps (the debug menu's input gate does it,
/// src/debug/input_gate.h), so a step always sees the same values from its start to its end, and a run replayed with
/// the same overrides file and the same input is the same run.
///
/// **Overrides file.** A text file of `category/name = value` lines (`#` starts a comment); save() writes the tunables
/// whose value differs from their default. An override for a tunable that is not registered yet is kept and applied
/// when it registers, so a file can be loaded at start-up before the subsystems exist.
class TunableRegistry {
  public:
    /// Registers a tunable over `target` (which must outlive its registration; remove() it before the variable dies).
    /// Its default is the variable's current value. A path registered twice is a programmer error (CONEY_ASSERT). A
    /// bool's range is 0 to 1; an int's and a float's default to the default value itself until range() is called.
    /// A waiting override for the path is queued at once.
    Tunable& add(std::string category, std::string name, bool* target);
    /// As above, for an int.
    Tunable& add(std::string category, std::string name, int* target);
    /// As above, for a float.
    Tunable& add(std::string category, std::string name, float* target);

    /// Unregisters the tunable at `path`; does nothing when there is none. Queued changes to it are dropped.
    void remove(std::string_view path);
    /// Unregisters every tunable of `category`; returns how many there were.
    std::size_t removeCategory(std::string_view category);

    /// The tunable at `path` (`category/name`), or null.
    [[nodiscard]] Tunable* find(std::string_view path);
    /// The categories, sorted.
    [[nodiscard]] std::vector<std::string> categories() const;
    /// The tunables of `category`, in registration order.
    [[nodiscard]] std::vector<Tunable*> inCategory(std::string_view category);
    /// Every tunable.
    [[nodiscard]] std::size_t size() const { return m_tunables.size(); }

    /// Queues `value` (clamped) for the tunable at `path`; applyPending() writes it. Returns false when there is no
    /// such tunable.
    bool set(std::string_view path, double value);
    /// Queues every tunable's default value.
    void resetAll();
    /// The value the tunable at `path` will have after applyPending(): the queued one, else the current one. Nothing
    /// when there is no such tunable.
    [[nodiscard]] std::optional<double> value(std::string_view path) const;
    /// Writes every queued change into its variable; returns how many there were. Call it between simulation steps.
    std::size_t applyPending();
    /// Changes queued and not yet applied.
    [[nodiscard]] std::size_t pending() const { return m_pending.size(); }

    /// The overrides file's text for the tunables whose value (after the queued changes) differs from the default,
    /// sorted by path; overrides read from a file for tunables not registered now are kept in it too.
    [[nodiscard]] std::string saveText() const;
    /// Reads an overrides file's text: each override is queued (or kept for a tunable not registered yet). Fails with
    /// ErrorCode::Invalid, naming the line, on a line that is not `path = number` (also `on`/`off`/`true`/`false`);
    /// nothing is queued then.
    [[nodiscard]] std::expected<std::size_t, Error> loadText(std::string_view text);
    /// saveText() written to the file `path`. Fails with ErrorCode::Io when the file cannot be written.
    [[nodiscard]] std::expected<void, Error> save(const std::string& path) const;
    /// loadText() of the file `path`. Fails with ErrorCode::NotFound when it does not exist, and as loadText() does.
    [[nodiscard]] std::expected<std::size_t, Error> load(const std::string& path);

  private:
    // Registers one tunable (the three add() overloads share it).
    Tunable& addTarget(std::string category, std::string name, Tunable::Target target);
    // The index of the tunable at `path`, or nothing.
    [[nodiscard]] std::optional<std::size_t> indexOf(std::string_view path) const;

    std::vector<std::unique_ptr<Tunable>> m_tunables;       // registration order; pointers stay valid
    std::map<std::string, double, std::less<>> m_pending;   // path -> value, queued by set()
    std::map<std::string, double, std::less<>> m_unclaimed; // overrides for tunables not registered yet
};

/// The registry the game's subsystems register with. Tests make their own TunableRegistry instead.
[[nodiscard]] TunableRegistry& globalTunables();

} // namespace coney::debug
