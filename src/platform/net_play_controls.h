// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "debug/net_controls.h"
#include "platform/net_play.h"

namespace coney::platform {

/// The debug menus' Network page over network play (debug::NetControls, docs/guides/debug-menu.md#pages): host, join,
/// leave, the settings, the figures and the simulated network, through NetPlay's own API. In the debug tools' target
/// (docs/guides/building.md#debug-tools), so the release build leaves it out.
class NetPlayControls final : public debug::NetControls {
  public:
    /// Controls of `play`, which must outlive them.
    explicit NetPlayControls(NetPlay& play) : m_play(play) {}

    [[nodiscard]] debug::NetStatus status() const override { return m_play.status(); }
    [[nodiscard]] std::string playerName() const override { return m_play.playerName(); }
    bool setPlayerName(const std::string& name) override { return m_play.setPlayerName(name); }
    [[nodiscard]] std::uint16_t port() const override { return m_play.port(); }
    void setPort(std::uint16_t port) override { m_play.setPort(port); }
    [[nodiscard]] std::string address() const override { return m_play.address(); }
    void setAddress(const std::string& address) override { m_play.setAddress(address); }
    bool host() override { return m_play.host(); }
    bool join() override { return m_play.join(); }
    void disconnect() override { m_play.disconnect(); }
    [[nodiscard]] debug::NetSimulation simulation() const override { return m_play.simulation(); }
    void setSimulation(const debug::NetSimulation& simulation) override { m_play.setSimulation(simulation); }
    [[nodiscard]] std::vector<std::string> log() const override { return m_play.log(); }

  private:
    NetPlay& m_play;
};

} // namespace coney::platform
