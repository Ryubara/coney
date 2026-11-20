// SPDX-License-Identifier: GPL-3.0-or-later
// Stub for the test commit: the level messages are not encoded yet.
#include <expected>

#include "core/error.h"
#include "net/bit_stream.h"
#include "net/protocol.h"
#include "net/protocol_wire.h"

namespace coney::net {

void writeLevelMessage(BitWriter& /*out*/, const LoadLevel& /*message*/) {}
void writeLevelMessage(BitWriter& /*out*/, const LevelLoaded& /*message*/) {}
void writeLevelMessage(BitWriter& /*out*/, const MissionEnd& /*message*/) {}
void writeLevelMessage(BitWriter& /*out*/, const MovieDone& /*message*/) {}
std::expected<Message, Error> readLoadLevelBody(BitReader& /*in*/) { return fail(ErrorCode::Invalid, "stub"); }
std::expected<Message, Error> readLevelLoadedBody(BitReader& /*in*/) { return fail(ErrorCode::Invalid, "stub"); }
std::expected<Message, Error> readMissionEndBody(BitReader& /*in*/) { return fail(ErrorCode::Invalid, "stub"); }
std::expected<Message, Error> readMovieDoneBody(BitReader& /*in*/) { return fail(ErrorCode::Invalid, "stub"); }

} // namespace coney::net
