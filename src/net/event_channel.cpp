// SPDX-License-Identifier: GPL-3.0-or-later
// Stubs for the tests' first run (the test commit): the next commit implements these.
#include "net/event_channel.h"

#include <cstdint>
#include <expected>
#include <span>
#include <vector>

#include "core/error.h"

namespace coney::net {

std::vector<NetEvent> toNetEvents(std::span<const GameEvent> /*events*/, const EventSubjectIds& /*ids*/) { return {}; }

std::vector<EventBatch> EventSender::build(std::uint32_t /*step*/, std::span<const NetEvent> /*events*/) { return {}; }

EventsApplied EventReceiver::seeOneShot(std::uint32_t /*sequence*/) { return EventsApplied::Applied; }

std::expected<EventsApplied, Error> EventReceiver::apply(const EventBatch& /*batch*/) { return EventsApplied::Applied; }

std::vector<ArrivedEvent> EventReceiver::take(std::uint32_t /*step*/) { return {}; }

} // namespace coney::net
