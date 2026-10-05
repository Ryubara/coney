// SPDX-License-Identifier: GPL-3.0-or-later
#include "debug/combat_tunables.h"

#include "combat/combat_tuning.h"

namespace coney::debug {

void registerCombatTunables(TunableRegistry& registry) {
    combat::CombatTuning& tuning = combat::combatTuning();
    constexpr const char* kCategory = "Combat";

    // Input: read every update by the command matcher and the square attack.
    registry.add(kCategory, "History hold", &tuning.historyHoldSamples)
        .range(1, 30, 1)
        .units("samples")
        .describe("Samples circle or triangle must be held for the hold commands (tackle)");
    registry.add(kCategory, "Snap attacks", &tuning.snapAttacks).describe("Square snaps to a side or back");

    // The chain attacks' timing, in updates from the press.
    registry.add(kCategory, "Hit update", &tuning.hitUpdate).range(0, 30, 1).units("updates");
    registry.add(kCategory, "Chain window opens", &tuning.chainOpenUpdate).range(0, 30, 1).units("updates");
    registry.add(kCategory, "Chain window closes", &tuning.chainCloseUpdate).range(0, 30, 1).units("updates");
    registry.add(kCategory, "Recovery starts", &tuning.recoveryUpdate).range(0, 30, 1).units("updates");
    registry.add(kCategory, "Attack ends", &tuning.attackEndUpdate).range(1, 60, 1).units("updates");

    // Grabs and the power meter.
    registry.add(kCategory, "Grab search scale", &tuning.grabSearchScale)
        .range(0.5, 2, 0.05)
        .describe("The grab and tackle search reach over the clip's far range");
    registry.add(kCategory, "Power endurance", &tuning.powerEndurance)
        .range(0, 1, 0.05)
        .describe("Power fraction a power strike needs and a throw costs");
    registry.add(kCategory, "Grab strike cost", &tuning.grabStrikeCost)
        .range(0, 1, 0.05)
        .describe("Power fraction a grab strike costs, halved for the player");
    registry.add(kCategory, "Power drain", &tuning.powerDrainPerSecond).range(0, 120, 1).units("/s");

    // Rage.
    registry.add(kCategory, "Rage points cap", &tuning.ragePointsCap).range(0, 100, 1);
    registry.add(kCategory, "Rage factor below cap", &tuning.rageFactorBelow).range(0, 2, 0.05);
    registry.add(kCategory, "Rage factor above cap", &tuning.rageFactorAbove).range(0, 2, 0.05);
    registry.add(kCategory, "Rage drain", &tuning.rageDrainPerSecond).range(0, 60, 0.02).units("/s");
    registry.add(kCategory, "Rage hold", &tuning.rageHoldMs).range(0, 20000, 100).units("ms");
    registry.add(kCategory, "Rage decay", &tuning.rageDecayPerSecond).range(0, 60, 0.1).units("/s");

    // The button mash.
    registry.add(kCategory, "Mash target", &tuning.mashTarget).range(1, 5000, 10);
    registry.add(kCategory, "Mash decay", &tuning.mashDecay).range(0, 200, 1).units("/update");
    registry.add(kCategory, "Mash press gain", &tuning.mashPressGain).range(0, 2000, 10);

    // The mugging.
    registry.add(kCategory, "Mugging stick", &tuning.muggingStick).range(0, 1, 0.05);
    registry.add(kCategory, "Mugging time", &tuning.muggingRequiredMs).range(0, 60000, 100).units("ms");
    registry.add(kCategory, "Mugging period", &tuning.muggingPeriodMs).range(0, 60000, 100).units("ms");
    registry.add(kCategory, "Mugging fail time", &tuning.muggingFailMs).range(0, 120000, 1000).units("ms");
    registry.add(kCategory, "Mugging tolerance", &tuning.muggingToleranceDegrees).range(0, 180, 1).units("deg");

    // The stereo theft.
    registry.add(kCategory, "Theft stick", &tuning.theftStick).range(0, 1, 0.05);
    registry.add(kCategory, "Theft step limit", &tuning.theftMaxStepDegrees).range(1, 180, 1).units("deg");
    registry.add(kCategory, "Theft stage pause", &tuning.theftStagePauseMs).range(0, 2000, 10).units("ms");
    registry.add(kCategory, "Theft stages", &tuning.theftStages).range(1, 10, 1);
}

} // namespace coney::debug
