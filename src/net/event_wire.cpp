// SPDX-License-Identifier: GPL-3.0-or-later
// Stubs for the tests' first run (the test commit): the next commit implements these.
#include <cstddef>
#include <expected>

#include "core/error.h"
#include "net/bit_stream.h"
#include "net/protocol.h"
#include "net/protocol_wire.h"

namespace coney::net {

void writeEventBatchBody(BitWriter& /*out*/, const EventBatch& /*batch*/) {}

std::expected<EventBatch, Error> readEventBatchBody(BitReader& /*in*/) {
    return fail(ErrorCode::Invalid, "not built yet");
}

std::size_t eventBits(const NetEvent& /*event*/) { return 0; }

} // namespace coney::net
