// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>

// The all-humans trace (`--trace-humans FILE`, docs/guides/running-play-level.md#tracing): one CSV row per human per
// update, with what a gang fight is judged by (where each human stands and faces, its goals, its target and who else
// attacks it, its attacks, blocks, grabs and falls), so a big fight can be compared with the original's by behaviour
// statistics (`coney-tools trace fight-report`). The columns the player's trace and the original's recorder share
// carry the same names and units. Coney's own tool; the original has none.
// Research: docs/research/feel.md#method, docs/research/ai/index.md#brain

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
    /// `power`, `stamina`, `state` (the record's state word as far as Coney keeps its states: grabbed 0x10 / 0x20,
    /// grabbing 0x40 / 0x80, mounted 0x800, blocking 0x8000, wounded 0x10000, arrested 0x20000, knocked out 0x40000,
    /// down 0x80000, stunned 0x100000), `phase` (the record's `+0x08`, the held flags: 0x1 wind-up, 0x2 chain, 0x4
    /// end), `down` (1 on the ground), `blocking`, `stunned`, `grabbed` (1 held or tackled), `grabbing` (1 holding
    /// someone), `attacking` (1 in a wind-up, chain window or end), `lock` (the number of the human it has locked
    /// on, -1 for none), `target` (the number of its brain's target, -1 for none), `goals` (how many are on the
    /// stack), `goal` (the top goal's type id, -1 for none), `goal0` to `goal5` (the stack's type ids from the
    /// bottom, -1 above the top), `react` (the reaction goal's type id, -1 for none), `actions` (how many are
    /// queued), `next_attack` and `attackable` (ms until it may attack again and may be attacked again), `warned`
    /// (attacks announced on it this update), `enemies` (its enemy list's length), `slots` (attackers holding a slot
    /// on it), `slots_max` (how many it allows), `active` (active-attacker places held on it), `space_up` and
    /// `space_down` (its spacing bytes, standing and down), `threat` (its threat response), `fight_updates` and
    /// `fight_active` (with the fight goal on top, its updates and 1 when it holds an active place; -1 otherwise),
    /// `g_alert` (-1: Coney does not model the gang's alert state), `g_tactic` (its gang's tactic as the original's
    /// vtable: 0x00543740 TacticConfront, 0x00543320 TacticAttack, 0x00544160 the attack command's; 1 for another
    /// tactic, a stand-in; 0 for none), `g_leader` and `g_chosen` (the numbers of its gang's leader and chosen
    /// target, -1 for none), `g_teamups` (`GangEnableAttackStrategies`), `g_order` (the team-up order running: always
    /// -1, as Coney builds none) and `brain_off` (1 while its brain is dead, `BrDead`).
    ///
    /// Coney's own columns: `target_dist` (metres to the target, -1 for none), `action_kind` (the front action: none,
    /// attack, move, move_to_human, play_anim, set_command, turn, take_step or other), `attack_kind` (the attack kind
    /// the front action presses, -1 when it is not an attack), `slot_held` (1 when it holds a slot on its target),
    /// `help_calls` and `helpers_sent` (the help calls it made and the HelpRespond goals it was sent so far,
    /// which stand for the original's `help-call` and `send-helper` hook logs) and `target_causes` (its target
    /// changes so far by cause, `pick=3;front=1`, ai::TargetCause, which stand for the `set-target` hook log).
    /// Research: docs/research/rumble.md#gang-battle-metrics, docs/research/gang-fights.md#set-target-causes,
    /// research/traces/fields.toml
    [[nodiscard]] static std::string_view header();

    /// The rows of step `step` (counted from 1), one per brain of `brains` in slot order, each ending in a newline.
    [[nodiscard]] std::string rows(std::uint64_t step, const Brains& brains);

  private:
    // The number of `human`, given on first sight.
    int idOf(const human::Human* human);

    std::map<const human::Human*, int> m_ids;
};

} // namespace coney::ai
