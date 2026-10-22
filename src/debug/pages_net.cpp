// SPDX-License-Identifier: GPL-3.0-or-later
// The Network page of the debug menus: host or join a network game, its figures, and a simulated bad network. Coney's
// own tool (no @orig): the original has no network play (docs/guides/multiplayer.md).
#include "debug/debug_pages.h"

#include <algorithm>
#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "debug/debug_session.h"
#include "debug/menu_model.h"
#include "debug/net_controls.h"

namespace coney::debug {

namespace {

// A byte count as people read it: bytes, KB or MB.
std::string byteText(std::uint64_t bytes) {
    if (bytes < 10'000) {
        return std::format("{} B", bytes);
    }
    if (bytes < 10'000'000) {
        return std::format("{:.1f} KB", static_cast<double>(bytes) / 1000.0);
    }
    return std::format("{:.1f} MB", static_cast<double>(bytes) / 1'000'000.0);
}

// A round trip in milliseconds, or a dash before the first answer.
std::string pingText(std::uint64_t roundTripNs) {
    return roundTripNs == 0 ? std::string("-") : std::format("{} ms", (roundTripNs + 500'000) / 1'000'000);
}

// The Simulate network submenu: one number per condition of NetSimulation, and a way back to a perfect link.
std::shared_ptr<MenuPage> simulationPage(NetControls* net) {
    auto page = std::make_shared<MenuPage>("Simulate network");
    // One whole-number setting of the simulation: read and written through `field`.
    const auto whole = [net](std::string label, std::uint32_t NetSimulation::* field, double max, double step,
                             std::string units, std::string help) {
        MenuItem item = numberItem(
            std::move(label), [net, field] { return static_cast<double>(net->simulation().*field); },
            [net, field](double value) {
                NetSimulation simulation = net->simulation();
                simulation.*field = static_cast<std::uint32_t>(std::max(0.0, value));
                net->setSimulation(simulation);
            },
            0.0, max, step, true);
        item.units = std::move(units);
        item.defaultValue = 0.0;
        item.help = std::move(help);
        return item;
    };
    // One percentage of the simulation.
    const auto percent = [net](std::string label, float NetSimulation::* field, std::string help) {
        MenuItem item = numberItem(
            std::move(label), [net, field] { return static_cast<double>(net->simulation().*field); },
            [net, field](double value) {
                NetSimulation simulation = net->simulation();
                simulation.*field = static_cast<float>(std::clamp(value, 0.0, 100.0));
                net->setSimulation(simulation);
            },
            0.0, 100.0, 1.0, false);
        item.units = "%";
        item.defaultValue = 0.0;
        item.help = std::move(help);
        return item;
    };
    page->add(whole("Latency", &NetSimulation::latencyMs, 1000.0, 10.0, "ms",
                    "Added to every packet this game sends, one way; set it on both ends for a symmetric link."));
    page->add(whole("Jitter", &NetSimulation::jitterMs, 500.0, 5.0, "ms",
                    "A further delay from 0 to this, drawn per packet."));
    page->add(percent("Loss", &NetSimulation::lossPercent,
                      "Unreliable packets dropped (pings, later snapshots); reliable ones are only delayed."));
    page->add(percent("Duplicates", &NetSimulation::duplicatePercent, "Unreliable packets sent twice."));
    page->add(percent("Reordering", &NetSimulation::reorderPercent,
                      "Unreliable packets held back 40 ms more, so later ones overtake them."));
    page->add(actionItem("Perfect link", [net] { net->setSimulation(NetSimulation{}); }))
        .withHelp("Every condition back to 0.");
    return page;
}

} // namespace

void addNetworkPage(DebugSession& session) {
    const std::function<NetControls*()> service = session.services().net;
    // Network play, asked for at each use; null in a run without it.
    const auto controls = [service]() -> NetControls* { return service ? service() : nullptr; };
    session.model().addChannel("Network/Ping (ms)", [controls] {
        const NetControls* net = controls();
        return net != nullptr ? static_cast<float>(net->status().roundTripNs) / 1'000'000.0F : 0.0F;
    });
    session.model().addPage(
        "Network",
        [controls](MenuPage& page) {
            NetControls* net = controls();
            if (net == nullptr) {
                page.add(watchItem("No network", [] { return std::string("this run has no network play"); }));
                return;
            }
            page.add(watchItem("Status", [net] { return net->status().state; }));

            // The settings Host and Join use.
            page.add(textItem(
                         "Player name", [net] { return net->playerName(); },
                         [net](const std::string& name) { (void)net->setPlayerName(name); }, false))
                .withHelp("The name the other players see: 1 to 32 printable ASCII characters. Also the server's "
                          "name when hosting.");
            MenuItem port = numberItem(
                "Port", [net] { return static_cast<double>(net->port()); },
                [net](double value) { net->setPort(static_cast<std::uint16_t>(std::clamp(value, 0.0, 65535.0))); }, 0.0,
                65535.0, 1.0, true);
            port.defaultValue = 27615.0;
            port.minText = "any free";
            port.help = "The UDP port Host listens on and Join connects to when the address names none.";
            page.add(std::move(port));
            page.add(textItem(
                         "Address", [net] { return net->address(); },
                         [net](const std::string& address) { net->setAddress(address); }, false))
                .withHelp("The server Join connects to: a host name or IPv4 address, optionally ADDR:PORT.");

            // The session.
            page.add(actionItem("Host", [net] { (void)net->host(); }))
                .withHelp("Host a game on Port (every interface); the others join this game's address.");
            page.add(actionItem("Join", [net] { (void)net->join(); })).withHelp("Join the game at Address.");
            page.add(actionItem("Disconnect", [net] { net->disconnect(); }))
                .withHelp("Leave the game, or stop hosting; everyone connected is told.");

            // The figures, live.
            page.add(watchItem(
                         "Ping", [net] { return pingText(net->status().roundTripNs); }, "Network/Ping (ms)"))
                .withHelp("The round trip, smoothed: to the server, or to the slowest player when hosting.");
            page.add(watchItem("Loss", [net] { return std::format("{:.0f} %", net->status().lossFraction * 100.0F); }))
                .withHelp("Pings unanswered among the last 32 (the newest 8 not yet judged).");
            page.add(watchItem("Input timing",
                               [net] {
                                   const NetStatus status = net->status();
                                   if (!status.inputLead) {
                                       return std::string("-");
                                   }
                                   return std::format("{} ahead (wants {}), clock {} %", *status.inputLead,
                                                      status.inputTarget, status.clockSpeedPercent);
                               }))
                .withHelp("Joined to a host: how many of this game's steps of input wait on the host, how many it "
                          "wants, and this game's clock, a little fast or slow to bring the one to the other.");
            page.add(watchItem("Sent", [net] {
                const NetStatus status = net->status();
                return std::format("{} in {} packets", byteText(status.bytesSent), status.packetsSent);
            }));
            page.add(watchItem("Received", [net] {
                const NetStatus status = net->status();
                return std::format("{} in {} packets", byteText(status.bytesReceived), status.packetsReceived);
            }));
            page.add(logItem("Players", [net] {
                std::vector<std::string> lines;
                for (const NetPlayerStatus& player : net->status().players) {
                    lines.push_back(std::format("{}: {} ({}, {:.0f} % loss, input {} ahead, {} lost)", player.slot + 1,
                                                player.name, pingText(player.roundTripNs), player.lossFraction * 100.0F,
                                                player.inputLead, player.inputLost));
                }
                return lines;
            }));
            page.add(submenuItem("Simulate network", [net] { return simulationPage(net); }))
                .withHelp("Latency, jitter, loss, duplicates and reordering on what this game sends.");
            page.add(logItem("Log", [net] { return net->log(); }));
        },
        "Network play: host or join a game, its ping, loss and bytes, and a simulated bad network.");
}

} // namespace coney::debug
