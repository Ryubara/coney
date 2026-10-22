// SPDX-License-Identifier: GPL-3.0-or-later
// The Network page driven through the menu model over a fake NetControls: no socket, no session.
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "debug/debug_session.h"
#include "debug/menu_model.h"
#include "debug/net_controls.h"

using coney::debug::DebugServices;
using coney::debug::DebugSession;
using coney::debug::MenuItem;
using coney::debug::NetControls;
using coney::debug::NetPlayerStatus;
using coney::debug::NetRole;
using coney::debug::NetSimulation;
using coney::debug::NetStatus;
using coney::debug::TunableRegistry;

namespace {

// Network play that only records what the page asked for and reports a host with one player.
class FakeNet final : public NetControls {
  public:
    [[nodiscard]] NetStatus status() const override {
        NetStatus status;
        status.role = hosting ? NetRole::Host : NetRole::Offline;
        status.state = hosting ? "hosting on port 27615, 1 player joined" : "offline";
        status.roundTripNs = hosting ? 42'000'000 : 0;
        status.lossFraction = 0.25F;
        status.bytesSent = 12'345;
        status.packetsSent = 7;
        if (hosting) {
            status.players.push_back(NetPlayerStatus{.slot = 1,
                                                     .name = "Swan",
                                                     .roundTripNs = 42'000'000,
                                                     .lossFraction = 0.0F,
                                                     .inputLead = 1,
                                                     .inputLost = 3});
        }
        status.inputLead = lead;
        status.inputTarget = 1;
        status.clockSpeedPercent = lead.value_or(1) < 1 ? 102 : 100;
        return status;
    }
    [[nodiscard]] std::string playerName() const override { return name; }
    bool setPlayerName(const std::string& value) override {
        name = value;
        return true;
    }
    [[nodiscard]] std::uint16_t port() const override { return portValue; }
    void setPort(std::uint16_t value) override { portValue = value; }
    [[nodiscard]] std::string address() const override { return addressValue; }
    void setAddress(const std::string& value) override { addressValue = value; }
    bool host() override {
        hosting = true;
        return true;
    }
    bool join() override {
        ++joins;
        return true;
    }
    void disconnect() override { hosting = false; }
    [[nodiscard]] NetSimulation simulation() const override { return simulated; }
    void setSimulation(const NetSimulation& value) override { simulated = value; }
    [[nodiscard]] std::vector<std::string> log() const override { return {"hosting on port 27615 as Player"}; }

    bool hosting = false;
    int joins = 0;
    std::optional<std::int64_t> lead; // a client's input lead, once the host has said it
    std::string name = "Player";
    std::uint16_t portValue = 27615;
    std::string addressValue = "127.0.0.1";
    NetSimulation simulated;
};

} // namespace

TEST_CASE("the Network page hosts, joins and sets its fields through NetControls", "[debug][net]") {
    TunableRegistry tunables;
    FakeNet net;
    DebugServices services;
    services.net = [&net]() -> NetControls* { return &net; };
    DebugSession session(tunables, services, nullptr);
    const auto page = session.model().openPage("Network");
    REQUIRE(page != nullptr);
    CHECK(page->find("Status")->watch() == "offline");
    CHECK(page->find("Ping")->watch() == "-");

    page->find("Player name")->setText("Cleon");
    CHECK(net.name == "Cleon");
    const MenuItem* port = page->find("Port");
    REQUIRE(port != nullptr);
    port->setNumber(30000.0);
    CHECK(net.portValue == 30000);
    CHECK(port->getNumber() == 30000.0);
    page->find("Address")->setText("10.0.0.2:30000");
    CHECK(net.addressValue == "10.0.0.2:30000");

    page->find("Host")->run();
    CHECK(net.hosting);
    CHECK(page->find("Status")->watch() == "hosting on port 27615, 1 player joined");
    CHECK(page->find("Ping")->watch() == "42 ms");
    CHECK(page->find("Loss")->watch() == "25 %");
    CHECK(page->find("Sent")->watch() == "12.3 KB in 7 packets");
    CHECK(page->find("Players")->lines() ==
          std::vector<std::string>{"2: Swan (42 ms, 0 % loss, input 1 ahead, 3 lost)"});
    CHECK(page->find("Input timing")->watch() == "-"); // a host has no host to send its input to
    net.lead = 0;
    CHECK(page->find("Input timing")->watch() == "0 ahead (wants 1), clock 102 %");
    CHECK(page->find("Log")->lines().size() == 1);
    page->find("Join")->run();
    CHECK(net.joins == 1);
    page->find("Disconnect")->run();
    CHECK_FALSE(net.hosting);
}

TEST_CASE("the Simulate network submenu sets every condition and puts them back", "[debug][net]") {
    TunableRegistry tunables;
    FakeNet net;
    DebugServices services;
    services.net = [&net]() -> NetControls* { return &net; };
    DebugSession session(tunables, services, nullptr);
    const auto page = session.model().openPage("Network");
    REQUIRE(page != nullptr);
    const auto simulate = page->find("Simulate network")->open();
    REQUIRE(simulate != nullptr);
    simulate->find("Latency")->setNumber(120.0);
    simulate->find("Jitter")->setNumber(30.0);
    simulate->find("Loss")->setNumber(5.0);
    simulate->find("Duplicates")->setNumber(150.0); // clamped to 100
    simulate->find("Reordering")->setNumber(2.0);
    CHECK(net.simulated.latencyMs == 120);
    CHECK(net.simulated.jitterMs == 30);
    CHECK(net.simulated.lossPercent == 5.0F);
    CHECK(net.simulated.duplicatePercent == 100.0F);
    CHECK(net.simulated.reorderPercent == 2.0F);
    simulate->find("Perfect link")->run();
    CHECK(net.simulated.latencyMs == 0);
    CHECK(net.simulated.lossPercent == 0.0F);
}

TEST_CASE("the Network page says so when the run has no network play", "[debug][net]") {
    TunableRegistry tunables;
    DebugSession session(tunables, DebugServices{}, nullptr);
    const auto page = session.model().openPage("Network");
    REQUIRE(page != nullptr);
    REQUIRE(page->items().size() == 1);
    CHECK(page->items().front().label == "No network");
}
