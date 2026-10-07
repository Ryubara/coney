// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <string>
#include <utility>

#include "scripting/script_system.h"
#include "warriors/created_humans.h"
#include "warriors/crime_reports.h"

namespace coney {

/// What a crime report reaches in a level Coney plays: the scripts' crime callback, the `CrimeScene` flag (moved by
/// `moveScene`, the objects' own mover), which humans are players, and a robbed store (`robStore`: its flag and alarm
/// strobe). **Coney's stand-in:** the gangs, the police spawners and the HUD are not wired yet, so hostility,
/// responders and the HUD's crime messages do nothing (open item on docs/research/crimes.md).
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
};

} // namespace coney
