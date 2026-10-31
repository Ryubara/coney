// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <vector>

#include "core/error.h"
#include "net/bit_stream.h"

// Network ids: how the server and its clients name a replicated thing (a human, a world object, a car) on the wire,
// since nothing crosses it as a pointer, a pool index or a script handle. Coney's own; the original has no network
// play (docs/guides/multiplayer.md#net-ids, docs/guides/network-architecture.md#net-ids).

namespace coney::net {

/// Bits of a NetId's generation on the wire.
inline constexpr int kNetIdGenerationBits = 6;
/// The last generation a slot takes before it wraps back to 1 (0 is never given: it marks "no id").
inline constexpr std::uint8_t kNetIdMaxGeneration = (1U << kNetIdGenerationBits) - 1U;
/// Bits of a NetId's slot on the wire.
inline constexpr int kNetIdSlotBits = 16;
/// The most slots a session can have: every slot kNetIdSlotBits can name.
inline constexpr std::size_t kNetIdMaxSlots = std::size_t{1} << kNetIdSlotBits;
/// Bits a NetId takes on the wire (writeNetId()).
inline constexpr int kNetIdBits = kNetIdSlotBits + kNetIdGenerationBits;

/// A replicated thing's id, given by the server (NetIdAllocator): a slot, and the slot's generation, so an id reused
/// after its thing has gone is never mistaken for the old one. The default id, generation 0, is "no id". Ids order by
/// slot, then generation, which is the order a table walks them in.
/// @coney-only network play; the original has none
struct NetId {
    std::uint16_t slot = 0;
    std::uint8_t generation = 0; ///< 1 to kNetIdMaxGeneration; 0 for "no id".

    /// Whether it names something (its generation is not 0).
    [[nodiscard]] constexpr bool valid() const { return generation != 0; }

    friend constexpr auto operator<=>(const NetId&, const NetId&) = default;
};

/// Writes `id` in kNetIdBits bits: the slot, then the generation. "No id" (NetId{}) may be written, for a field that
/// names nothing. A generation over kNetIdMaxGeneration is a programmer error (CONEY_ASSERT).
/// @coney-only network play; the original has none
void writeNetId(BitWriter& writer, NetId id);

/// Reads a writeNetId() id. Every value the bits can hold is an id, so only a short read fails (Truncated); a reader
/// that needs an id checks valid().
/// @coney-only network play; the original has none
[[nodiscard]] std::expected<NetId, Error> readNetId(BitReader& reader);

/// The server's ids: gives each thing that becomes replicated an id, and takes it back in two steps, so a slot is
/// reused only after every client has let go of it, and then only with a new generation.
///
/// A slot is free, live (allocate()) or retired (retire(): its thing has gone, but a client may still name it until it
/// acknowledges the removal, release()). allocate() takes the slots never used first, in order, and then the released
/// ones, oldest released first, so a slot rests as long as it can before it comes back. Plain data with no clock and
/// no randomness: two servers given the same calls give the same ids. Simulation state, owned by the session's game
/// mode.
/// @coney-only network play; the original has none
class NetIdAllocator {
  public:
    /// An allocator of at most `capacity` slots, 1 to kNetIdMaxSlots (CONEY_ASSERT).
    explicit NetIdAllocator(std::size_t capacity = kNetIdMaxSlots);

    /// A new live id. Fails with Invalid when every slot is live or retired.
    [[nodiscard]] std::expected<NetId, Error> allocate();
    /// Marks the thing `id` names as gone: the id is no longer live, and its slot waits for release(). `id` must be
    /// live (CONEY_ASSERT): the server retires only what it allocated.
    void retire(NetId id);
    /// Frees the slot of the retired `id` once every client has acknowledged its removal; the slot's next id has the
    /// next generation (after kNetIdMaxGeneration, 1). False, and nothing changes, when `id` is not retired (a repeated
    /// acknowledgement, or an id of another generation), since acknowledgements come from the clients.
    bool release(NetId id);

    /// Whether `id` is live: allocated in this generation and not retired.
    [[nodiscard]] bool live(NetId id) const;
    /// Ids live now.
    [[nodiscard]] std::size_t liveCount() const { return m_liveCount; }
    /// Slots it may use.
    [[nodiscard]] std::size_t capacity() const { return m_capacity; }

  private:
    // What a slot is doing.
    enum class SlotState : std::uint8_t { Free, Live, Retired };
    // One slot's state and its current (or, when free, its last) generation; 0 for a slot never used.
    struct Slot {
        SlotState state = SlotState::Free;
        std::uint8_t generation = 0;
    };

    std::size_t m_capacity = 0;
    std::vector<Slot> m_slots;            // the slots used so far, by slot number
    std::deque<std::uint16_t> m_released; // released slots, oldest first
    std::size_t m_liveCount = 0;
};

} // namespace coney::net
