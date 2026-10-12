// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>

// The all-humans trace (`--trace-humans FILE`, docs/guides/building.md#tracing): one CSV row per human per update,
// with what a gang fight is judged by (where each human stands and faces, its goals, its target and who else attacks
// it, its attacks, blocks, grabs and falls), so a big fight can be compared with the original's by behaviour
// statistics (`coney-tools trace fight-report`). The columns the player's trace and the original's recorder share
// carry the same names and units. Coney's own tool; the original has none.
// Research: docs/research/feel.md#method, docs/research/ai.md#brain

namespace coney::human {
class Human;
} // namespace coney::human

namespace coney::ai {

class Brains;

/// Writes the rows of the all-humans trace. It numbers the humans in the order it first sees them (the player's brain
/// is slot 0, so it is human 0), so a human keeps its number when another leaves the scene.
class FightTracer {
  public:
    /// The CSV header line, ending in a newline: the names of rows()'s columns.
    ///
    /// The columns the original's all-humans recorder also has (research/traces/fields.toml, `[[humans]]`) carry its
    /// names and units: `step`, `human` (its number), `player` (the player number the pad drives, 0 for player 1, -1
    /// for none), `btype` (its brain's type), `gang` (its gang's id, -1 for none), `x`, `y`, `z` (metres), `heading`
    /// (degrees, 0 faces +y), `speed` (m/s), `vz`, `gait` (0-5), `clip` (the anim id playing), `clip_frame` (the
    /// clip's frame, 30 a second), `command` (the command of this update, 0 for none), `health`, `health_max`,
    /// `power`, `stamina`, `down` (1 on the ground), `blocking`, `grabbed` (1 held or tackled), `grabbing` (1 holding
    /// someone), `lock` (the number of the human it has locked on, -1 for none), `target` (the number of its brain's
    /// target, -1 for none), `goals` (how many are on the stack), `goal` (the top goal's type id, -1 for none),
    /// `goal0` to `goal5` (the stack's type ids from the bottom, -1 above the top), `react` (the reaction goal's type
    /// id, -1 for none), `actions` (how many are queued), `next_attack` and `attackable` (ms until it may attack
    /// again and may be attacked again), `warned` (attacks announced on it this update), `enemies` (its enemy list's
    /// length), `slots` (attackers holding a slot on it), `slots_max` (how many it allows), `threat` (its threat
    /// response), `g_tactic` (1 when its gang has a tactic), `g_leader` and `g_chosen` (the numbers of its gang's
    /// leader and chosen target, -1 for none).
    ///
    /// Coney's own columns: `target_dist` (metres to the target, -1 for none), `action_kind` (the front action: none,
    /// attack, move, move_to_human, play_anim, set_command, turn, take_step or other), `attack_kind` (the attack kind
    /// the front action presses, -1 when it is not an attack) and `slot_held` (1 when it holds a slot on its target).
    [[nodiscard]] static std::string_view header();

    /// The rows of step `step` (counted from 1), one per brain of `brains` in slot order, each ending in a newline.
    [[nodiscard]] std::string rows(std::uint64_t step, const Brains& brains);

  private:
    // The number of `human`, given on first sight.
    int idOf(const human::Human* human);

    std::map<const human::Human*, int> m_ids;
};

} // namespace coney::ai
