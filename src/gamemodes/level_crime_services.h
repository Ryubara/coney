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

/// What a crime report reaches in a level Coney plays: the scripts' crime callback, the `CrimeScene` flag (moved by
/// `moveScene`, the objects' own mover), which humans are players, a robbed store (`robStore`: its flag and alarm
/// strobe), and the offender's gang and player 1's through the level's brains (setBrains()), so a report makes the
/// gang wanted. **Coney's stand-in:** the police's hostility, the police spawners and the HUD are not wired yet, so
/// hostility, responders and the HUD's crime messages do nothing (open item on docs/research/crimes.md).
///
/// Research: docs/research/ai.md#crimes
class LevelCrimeServices final : public CrimeServices {
  public:
    /// Over `scripts`, which must outlive it; `moveScene` moves the `CrimeScene` flag.
    LevelCrimeServices(script::ScriptSystem& scripts, std::function<void(const CrimePosition&)> moveScene,
                       std::function<void(const CrimePosition&, int)> robStore = {})
        : m_scripts(scripts), m_moveScene(std::move(moveScene)), m_robStore(std::move(robStore)) {}

    /// The humans the level scripts made (null: none is a player); must outlive its use.
    void setHumans(CreatedHumans* humans) { m_humans = humans; }
    /// The level's brains, by handle and player 1's (null: none, so no offender has a gang); must outlive its use.
    void setBrains(const ai::ScriptServices* brains) { m_brains = brains; }

    /// The gang of the human with `handle`, through his brain.
    [[nodiscard]] std::optional<CrimeGang> gangOf(double handle) override;
    /// Player 1's brain's gang (-1: none).
    [[nodiscard]] int playerOneGang() override;

    [[nodiscard]] bool isPlayer(double handle) override;
    /// Calls the Lua function `function` with the gang and the type.
    void callCrimeCallback(const std::string& function, int gang, int type) override;
    void moveCrimeScene(const CrimePosition& at) override;
    /// Hands the break-in to `robStore` (the objects' services: the store's alarm strobe).
    void markStoreRobbed(const CrimePosition& at, int offenderGang) override;

  private:
    script::ScriptSystem& m_scripts;
    std::function<void(const CrimePosition&)> m_moveScene;
    std::function<void(const CrimePosition&, int)> m_robStore;
    CreatedHumans* m_humans = nullptr;
    const ai::ScriptServices* m_brains = nullptr;
};

} // namespace coney
