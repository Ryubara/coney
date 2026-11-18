// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/net_game.h"

#include <cstdint>
#include <memory>
#include <string_view>

#include "platform/net_play.h"

// Stubs for NetGame's tests: the commit after this one gives NetGame its files, its replay and its record.

namespace coney::platform {

std::expected<std::unique_ptr<NetGame>, Error> NetGame::open(const Options& /*options*/) {
    return std::unique_ptr<NetGame>(new NetGame());
}

PortSamples NetGame::sample(std::uint64_t frame) { return m_inner != nullptr ? m_inner->sample(frame) : PortSamples{}; }

void NetGame::attach(NetPlay& /*net*/) {}

void NetGame::afterStep(NetPlay& /*net*/) {}

void NetGame::note(std::string_view /*line*/) {}

} // namespace coney::platform
