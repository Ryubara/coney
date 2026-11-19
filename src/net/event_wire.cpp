// SPDX-License-Identifier: GPL-3.0-or-later
// The EventBatch's body on the wire (docs/guides/multiplayer.md#events): the layout EventBatch's comment gives, with
// each event writing only the fields its kind carries, so a one-shot sound costs a few bytes.
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <initializer_list>
#include <string>
#include <utility>

#include "core/assert.h"
#include "core/error.h"
#include "core/game_events.h"
#include "net/bit_stream.h"
#include "net/net_id.h"
#include "net/protocol.h"
#include "net/protocol_wire.h"

namespace coney::net {

namespace {

// Whether the fields `event`'s kind does not carry are at their defaults: one encoding per event.
bool unusedFieldsClear(const NetEvent& event) {
    const std::uint8_t fields = gameEventFields(event.kind);
    return ((fields & kEventSubject) != 0 || !event.subject.valid()) && ((fields & kEventId) != 0 || event.id == 0) &&
           ((fields & kEventValue) != 0 || event.value == 0) && ((fields & kEventExtra) != 0 || event.extra == 0) &&
           ((fields & kEventPosition) != 0 || event.position == std::array<float, 3>{}) &&
           ((fields & kEventRotation) != 0 || event.rotation == std::array<float, 4>{0.0F, 0.0F, 0.0F, 1.0F}) &&
           ((fields & kEventText) != 0 || event.text.empty());
}

// Writes one event: its kind, its ordinal, then the fields its kind carries.
void writeEvent(BitWriter& out, const NetEvent& event) {
    CONEY_ASSERT(unusedFieldsClear(event));
    CONEY_ASSERT(event.text.size() <= kMaxGameEventText);
    out.writeU8(static_cast<std::uint8_t>(event.kind));
    out.writeVarUint(event.ordinal);
    const std::uint8_t fields = gameEventFields(event.kind);
    if ((fields & kEventSubject) != 0) {
        writeNetId(out, event.subject);
    }
    if ((fields & kEventId) != 0) {
        out.writeVarUint(event.id);
    }
    if ((fields & kEventValue) != 0) {
        out.writeVarUint(event.value);
    }
    if ((fields & kEventExtra) != 0) {
        out.writeVarUint(event.extra);
    }
    if ((fields & kEventPosition) != 0) {
        for (const float coordinate : event.position) {
            out.writeF32(coordinate);
        }
    }
    if ((fields & kEventRotation) != 0) {
        for (const float component : event.rotation) {
            out.writeF32(component);
        }
    }
    if ((fields & kEventText) != 0) {
        out.writeString(event.text, kMaxGameEventText);
    }
}

// Reads one writeEvent() event.
std::expected<NetEvent, Error> readEvent(BitReader& in) {
    auto kind = in.readU8();
    if (!kind) {
        return std::unexpected(kind.error());
    }
    if (!validGameEventKind(*kind)) {
        return fail(ErrorCode::Invalid, "network event has an unknown kind " + std::to_string(*kind));
    }
    NetEvent event;
    event.kind = static_cast<GameEventKind>(*kind);
    auto ordinal = in.readVarUint();
    if (!ordinal) {
        return std::unexpected(ordinal.error());
    }
    if (*ordinal > 0xffffU) {
        return fail(ErrorCode::Invalid, "network event ordinal is over 16 bits");
    }
    event.ordinal = static_cast<std::uint16_t>(*ordinal);
    const std::uint8_t fields = gameEventFields(event.kind);
    if ((fields & kEventSubject) != 0) {
        auto subject = readNetId(in);
        if (!subject) {
            return std::unexpected(subject.error());
        }
        event.subject = *subject;
    }
    // id, value and extra in that order, each only when the kind carries it.
    for (const auto [field, target] :
         {std::pair{kEventId, &event.id}, std::pair{kEventValue, &event.value}, std::pair{kEventExtra, &event.extra}}) {
        if ((fields & field) == 0) {
            continue;
        }
        auto number = readSequence(in);
        if (!number) {
            return std::unexpected(number.error());
        }
        *target = *number;
    }
    if ((fields & kEventPosition) != 0) {
        for (float& coordinate : event.position) {
            auto value = in.readF32();
            if (!value) {
                return std::unexpected(value.error());
            }
            coordinate = *value;
        }
    }
    if ((fields & kEventRotation) != 0) {
        for (float& component : event.rotation) {
            auto value = in.readF32();
            if (!value) {
                return std::unexpected(value.error());
            }
            component = *value;
        }
    }
    if ((fields & kEventText) != 0) {
        auto text = in.readString(kMaxGameEventText);
        if (!text) {
            return std::unexpected(text.error());
        }
        event.text = std::move(*text);
    }
    return event;
}

} // namespace

void writeEventBatchBody(BitWriter& out, const EventBatch& batch) {
    CONEY_ASSERT(batch.sequence != 0);
    CONEY_ASSERT(!batch.events.empty() && batch.events.size() <= kMaxBatchEvents);
    out.writeBool(batch.reliable);
    out.writeVarUint(batch.sequence);
    out.writeVarUint(batch.step);
    out.writeU8(static_cast<std::uint8_t>(batch.events.size() - 1));
    for (std::size_t i = 0; i < batch.events.size(); ++i) {
        const NetEvent& event = batch.events[i];
        CONEY_ASSERT(reliableGameEvent(event.kind) == batch.reliable);
        CONEY_ASSERT(i == 0 || event.ordinal > batch.events[i - 1].ordinal);
        writeEvent(out, event);
    }
}

std::expected<EventBatch, Error> readEventBatchBody(BitReader& in) {
    EventBatch batch;
    auto reliable = in.readBool();
    if (!reliable) {
        return std::unexpected(reliable.error());
    }
    batch.reliable = *reliable;
    auto sequence = readSequence(in);
    if (!sequence) {
        return std::unexpected(sequence.error());
    }
    if (*sequence == 0) {
        return fail(ErrorCode::Invalid, "network event batch has sequence 0");
    }
    batch.sequence = *sequence;
    auto step = readSequence(in);
    if (!step) {
        return std::unexpected(step.error());
    }
    batch.step = *step;
    auto countLess1 = in.readU8();
    if (!countLess1) {
        return std::unexpected(countLess1.error());
    }
    const std::size_t count = std::size_t{*countLess1} + 1;
    batch.events.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        auto event = readEvent(in);
        if (!event) {
            return std::unexpected(event.error());
        }
        if (reliableGameEvent(event->kind) != batch.reliable) {
            return fail(ErrorCode::Invalid, "network event batch mixes reliable and one-shot events");
        }
        if (!batch.events.empty() && event->ordinal <= batch.events.back().ordinal) {
            return fail(ErrorCode::Invalid, "network event batch's ordinals do not rise");
        }
        batch.events.push_back(std::move(*event));
    }
    return batch;
}

std::size_t eventBits(const NetEvent& event) {
    BitWriter out;
    writeEvent(out, event);
    return out.bitCount();
}

} // namespace coney::net
