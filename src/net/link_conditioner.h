// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "core/error.h"
#include "net/transport.h"

namespace coney::net {

/// The bad network a LinkConditioner plays. All zero (the default) is a perfect link.
struct LinkConditions {
    std::uint32_t latencyMs = 0;    ///< Added to every packet this end sends, one way.
    std::uint32_t jitterMs = 0;     ///< A further delay drawn evenly from 0 to this, per packet.
    float lossFraction = 0.0F;      ///< The chance, 0 to 1, that an unreliable packet is dropped.
    float duplicateFraction = 0.0F; ///< The chance, 0 to 1, that an unreliable packet is sent twice.
    float reorderFraction = 0.0F;   ///< The chance, 0 to 1, that an unreliable packet is held back reorderDelayMs more.
    std::uint32_t reorderDelayMs = 40; ///< How long a reordered packet is held back.

    /// Whether these are a perfect link (nothing added).
    [[nodiscard]] bool perfect() const {
        return latencyMs == 0 && jitterMs == 0 && lossFraction <= 0.0F && duplicateFraction <= 0.0F &&
               reorderFraction <= 0.0F;
    }
};

/// A Transport in front of another that makes what this end sends arrive late, out of order, twice or never, for
/// testing the sessions on a bad network: the tests in front of the loopback, the Network page's Simulate network
/// menu in front of ENet. Coney's own test aid (docs/guides/multiplayer.md#link-conditioner).
///
/// Only sending is conditioned, so conditions on one end add their latency to the round trip once; condition both
/// ends for a symmetric link. The delivery contracts still hold: a ReliableOrdered packet is only delayed, never
/// lost, duplicated or overtaken by another reliable one (a real transport resends what is lost, which looks like
/// delay), and an UnreliableSequenced packet that would arrive after a newer one is dropped. Its random draws come
/// from its own xorshift generator from a fixed seed, never from the game's (GameRandom), so a run's network
/// conditions never change the game's random numbers, and a test's conditions are the same on every run.
class LinkConditioner final : public Transport {
  public:
    /// Conditions what is sent through `inner`, which must outlive this.
    explicit LinkConditioner(Transport& inner, std::uint32_t seed = 0x2545f491U);

    /// Changes the conditions from now on; packets already held keep their release times.
    void setConditions(const LinkConditions& conditions) { m_conditions = conditions; }
    [[nodiscard]] const LinkConditions& conditions() const { return m_conditions; }
    /// Packets held back now.
    [[nodiscard]] std::size_t heldPackets() const { return m_held.size(); }
    /// Unreliable packets dropped so far: by the loss draw, or as sequenced packets overtaken by a newer one.
    [[nodiscard]] std::uint64_t droppedPackets() const { return m_dropped; }

    [[nodiscard]] std::expected<void, Error> listen(const ListenConfig& config) override {
        return m_inner.listen(config);
    }
    [[nodiscard]] std::expected<PeerId, Error> connect(std::string_view address, std::uint16_t port) override {
        return m_inner.connect(address, port);
    }
    /// Holds the packet until its delay has passed (in advance() and flush()), or drops it.
    [[nodiscard]] std::expected<void, Error> send(PeerId peer, Delivery delivery,
                                                  std::span<const std::byte> data) override;
    /// Moves this end's time on and sends what is due.
    void advance(std::uint64_t elapsedNs) override;
    /// Passes the inner transport's events through, noting which connections are up.
    [[nodiscard]] std::optional<TransportEvent> poll() override;
    /// Sends what is due, then flushes the inner transport.
    void flush() override;
    /// Closes the connection once its held reliable packets are sent; held unreliable ones are dropped then.
    void disconnect(PeerId peer) override;
    /// True once no connection is up or closing here (a closing one waits for its held reliable packets) and the
    /// transport underneath is closed too.
    [[nodiscard]] bool closed() const override { return m_peers.empty() && m_inner.closed(); }
    [[nodiscard]] std::uint16_t boundPort() const override { return m_inner.boundPort(); }
    [[nodiscard]] TransportStats stats() const override { return m_inner.stats(); }

  private:
    // A packet waiting for its release time.
    struct Held {
        std::uint64_t releaseNs = 0;
        std::uint64_t order = 0; // send order, which breaks ties and orders sequenced packets
        PeerId peer = 0;
        Delivery delivery = Delivery::Unreliable;
        std::vector<std::byte> data;
    };
    // What the conditioner tracks per connection.
    struct PeerState {
        std::uint64_t lastReliableReleaseNs = 0; // reliable packets never overtake each other
        std::uint64_t lastSequencedOrder = 0;    // the newest sequenced packet sent on; older ones are dropped
        bool closing = false;                    // disconnect() was called
    };

    // A draw in [0, 1) from the generator.
    [[nodiscard]] float draw();
    // The delay for one packet: the latency plus a jitter draw, in nanoseconds.
    [[nodiscard]] std::uint64_t delayNs();
    // Sends every held packet whose time has come, in release order, then closes connections that are done.
    void release();

    Transport& m_inner;
    LinkConditions m_conditions;
    std::uint32_t m_state; // xorshift32 state; never 0
    std::uint64_t m_nowNs = 0;
    std::uint64_t m_nextOrder = 1;
    std::uint64_t m_dropped = 0;
    std::vector<Held> m_held;
    std::map<PeerId, PeerState> m_peers; // connections that are up
};

} // namespace coney::net
