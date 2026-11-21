// SPDX-License-Identifier: GPL-3.0-or-later
// The level messages' encoding (docs/guides/multiplayer.md#levels): LoadLevel, LevelLoaded, MissionEnd and MovieDone,
// as protocol.h lays each out. Coney's own; the original has no network play.
#include <cstdint>
#include <expected>
#include <string>
#include <utility>

#include "core/assert.h"
#include "core/error.h"
#include "net/bit_stream.h"
#include "net/protocol.h"
#include "net/protocol_wire.h"

namespace coney::net {

namespace {

// Reads a load number: a sequence that is never 0, since loads count from 1.
std::expected<std::uint32_t, Error> readLoad(BitReader& in, const char* what) {
    auto load = readSequence(in);
    if (!load) {
        return std::unexpected(load.error());
    }
    if (*load == 0) {
        return fail(ErrorCode::Invalid, std::string("network ") + what + " names load 0");
    }
    return *load;
}

} // namespace

void writeLevelMessage(BitWriter& out, const LoadLevel& message) {
    CONEY_ASSERT(message.load != 0 && message.checkpoint != 0);
    CONEY_ASSERT(validLevelName(message.level));
    out.writeU8(static_cast<std::uint8_t>(MessageId::LoadLevel));
    out.writeVarUint(message.load);
    out.writeU8(static_cast<std::uint8_t>(message.reason));
    out.writeU16(message.checkpoint);
    out.writeString(message.level, kMaxLevelNameBytes);
}

void writeLevelMessage(BitWriter& out, const LevelLoaded& message) {
    CONEY_ASSERT(message.load != 0);
    out.writeU8(static_cast<std::uint8_t>(MessageId::LevelLoaded));
    out.writeVarUint(message.load);
}

void writeLevelMessage(BitWriter& out, const MissionEnd& message) {
    CONEY_ASSERT(message.load != 0);
    out.writeU8(static_cast<std::uint8_t>(MessageId::MissionEnd));
    out.writeVarUint(message.load);
    out.writeU8(static_cast<std::uint8_t>(message.outcome));
}

void writeLevelMessage(BitWriter& out, const MovieDone& message) {
    out.writeU8(static_cast<std::uint8_t>(MessageId::MovieDone));
    out.writeVarUint(message.movieStep);
    out.writeBool(message.skipped);
}

std::expected<Message, Error> readLoadLevelBody(BitReader& in) {
    auto load = readLoad(in, "level order");
    if (!load) {
        return std::unexpected(load.error());
    }
    auto reason = in.readU8();
    if (!reason) {
        return std::unexpected(reason.error());
    }
    auto checkpoint = in.readU16();
    if (!checkpoint) {
        return std::unexpected(checkpoint.error());
    }
    auto level = in.readString(kMaxLevelNameBytes);
    if (!level) {
        return std::unexpected(level.error());
    }
    if (*reason < static_cast<std::uint8_t>(LoadReason::Change) ||
        *reason > static_cast<std::uint8_t>(LoadReason::Join)) {
        return fail(ErrorCode::Invalid, "network level order has an unknown reason");
    }
    // The original's checkpoints count from 1 (docs/research/characters.md#level-starts).
    if (*checkpoint == 0) {
        return fail(ErrorCode::Invalid, "network level order names checkpoint 0");
    }
    // A name off the network is never a path or a byte no level name has (validLevelName()).
    if (!validLevelName(*level)) {
        return fail(ErrorCode::Invalid, "network level order names no level, or a name with a foreign character");
    }
    return LoadLevel{.load = *load,
                     .level = std::move(*level),
                     .checkpoint = *checkpoint,
                     .reason = static_cast<LoadReason>(*reason)};
}

std::expected<Message, Error> readLevelLoadedBody(BitReader& in) {
    auto load = readLoad(in, "level loaded");
    if (!load) {
        return std::unexpected(load.error());
    }
    return LevelLoaded{.load = *load};
}

std::expected<Message, Error> readMissionEndBody(BitReader& in) {
    auto load = readLoad(in, "mission end");
    if (!load) {
        return std::unexpected(load.error());
    }
    auto outcome = in.readU8();
    if (!outcome) {
        return std::unexpected(outcome.error());
    }
    if (*outcome < static_cast<std::uint8_t>(MissionOutcome::Passed) ||
        *outcome > static_cast<std::uint8_t>(MissionOutcome::Quit)) {
        return fail(ErrorCode::Invalid, "network mission end has an unknown outcome");
    }
    return MissionEnd{.load = *load, .outcome = static_cast<MissionOutcome>(*outcome)};
}

std::expected<Message, Error> readMovieDoneBody(BitReader& in) {
    auto step = readSequence(in);
    if (!step) {
        return std::unexpected(step.error());
    }
    auto skipped = in.readBool();
    if (!skipped) {
        return std::unexpected(skipped.error());
    }
    return MovieDone{.movieStep = *step, .skipped = *skipped};
}

} // namespace coney::net
