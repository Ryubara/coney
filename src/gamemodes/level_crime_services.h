// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <utility>

#include "ai/script_services.h"
#include "scripting/script_system.h"
#include "warriors/created_humans.h"
#include "warriors/crime_reports.h"

namespace coney {

/// The store fronts and the statistics a crime report reaches through the level's objects' services.
struct CrimeStoreHooks {
    /// The store front within 10 m of a break-in (CrimeServices::nearestStore()).
    std::function<std::optional<CrimeStore>(const CrimePosition&)> nearest;
    /// A first break-in's store (CrimeServices::robStore()).
    std::function<void(const CrimePosition&)> rob;
    /// A robbed store's new gang (CrimeServices::claimStore()).
    std::function<void(const CrimePosition&, int)> claim;
    /// One crime event for a player human (CrimeServices::scoreCrime()).
    std::function<void(double, int)> scoreCrime;
};

/// What a crime report reaches in a level Coney plays: the scripts' crime callback, the `CrimeScene` flag (moved by
/// `moveScene`, the objects' own mover), which humans are players, the store fronts and the crime statistics
/// (`stores`), and the offender's gang, its leader, a victim's brain kind and player 1's gang through the level's
/// brains (setBrains()), so a report makes the gang wanted, and player 1's crime messages to the HUD (setHud()).
/// **Coney's stand-in:** the police's hostility and the police spawners are not wired yet, so hostility and responders
/// do nothing (open item on docs/research/crimes.md).
///
/// Research: docs/research/ai-gangs.md#crimes
class LevelCrimeServices final : public CrimeServices {
  public:
    /// Over `scripts`, which must outlive it; `moveScene` moves the `CrimeScene` flag.
    LevelCrimeServices(script::ScriptSystem& scripts, std::function<void(const CrimePosition&)> moveScene,
                       CrimeStoreHooks stores = {})
        : m_scripts(scripts), m_moveScene(std::move(moveScene)), m_stores(std::move(stores)) {}

    /// The humans the level scripts made (null: none is a player); must outlive its use.
    void setHumans(CreatedHumans* humans) { m_humans = humans; }
    /// The level's brains, by handle and player 1's (null: none, so no offender has a gang); must outlive its use.
    void setBrains(const ai::ScriptServices* brains) { m_brains = brains; }
    /// Where player 1's crime messages go (`HUD_SetWanted`'s message; empty: nowhere).
    void setHud(std::function<void(int message)> notify) { m_notifyHud = std::move(notify); }

    /// The gang of the human with `handle`, through his brain.
    [[nodiscard]] std::optional<CrimeGang> gangOf(double handle) override;
    /// Player 1's brain's gang (-1: none).
    [[nodiscard]] int playerOneGang() override;

    /// The player index of a human the level made as a player (1 or 2 made: 0 or 1).
    /// @coney-only the crime report's seam to the level's humans, brains and objects
    [[nodiscard]] int playerIndex(double handle) override;
    /// The brain kind of the human with `handle`, through his brain.
    /// @coney-only the crime report's seam to the level's humans, brains and objects
    [[nodiscard]] int brainType(double handle) override;
    /// The leader of the offender's gang, through the offender's brain.
    /// @coney-only the crime report's seam to the level's humans, brains and objects
    [[nodiscard]] std::optional<double> gangLeader(double offender) override;
    /// Calls the Lua function `function` with the gang and the type.
    void callCrimeCallback(const std::string& function, int gang, int type) override;
    /// Hands the move to `moveScene`.
    void moveCrimeScene(const CrimePosition& at) override;
    /// The store hooks' answer (none without them).
    /// @coney-only the crime report's seam to the level's humans, brains and objects
    [[nodiscard]] std::optional<CrimeStore> nearestStore(const CrimePosition& at) override;
    /// Hands the break-in to the store hooks.
    /// @coney-only the crime report's seam to the level's humans, brains and objects
    void robStore(const CrimePosition& at) override;
    /// Hands the store's new gang to the store hooks.
    /// @coney-only the crime report's seam to the level's humans, brains and objects
    void claimStore(const CrimePosition& at, int gang) override;
    /// Hands the crime event to the store hooks' statistics.
    /// @coney-only the crime report's seam to the level's humans, brains and objects
    void scoreCrime(double human, int event) override;
    /// Hands the message to setHud()'s receiver.
    void notifyHud(int message) override {
        if (m_notifyHud) {
            m_notifyHud(message);
        }
    }

  private:
    script::ScriptSystem& m_scripts;
    std::function<void(const CrimePosition&)> m_moveScene;
    CrimeStoreHooks m_stores;
    CreatedHumans* m_humans = nullptr;
    const ai::ScriptServices* m_brains = nullptr;
    std::function<void(int)> m_notifyHud;
};

} // namespace coney
