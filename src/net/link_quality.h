// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

namespace coney::net {

/// The round trip and loss of one connection, measured by the sessions' own Ping and Pong messages, so the figures
/// are the same over every transport (ENet's own are not visible through the loopback). Times are in the sessions'
/// nanoseconds, the sum of what advance() was told; nothing here reads a clock. Coney's own
/// (docs/guides/multiplayer.md#link-quality).
class LinkQuality {
  public:
    /// Pings remembered for the loss figure.
    static constexpr std::uint32_t kWindow = 32;
    /// A ping counts as lost only once this many newer pings were sent and it is still unanswered, so a slow answer
    /// is not taken for a lost one.
    static constexpr std::uint32_t kGrace = 8;

    /// Records a ping sent at `nowNs`; returns the sequence to put in it.
    [[nodiscard]] std::uint32_t pingSent(std::uint64_t nowNs);
    /// Records the Pong for `sequence` arriving at `nowNs`. An answer to an unknown, forgotten or already answered
    /// ping (a duplicate, or a forged one) is ignored.
    void pongReceived(std::uint32_t sequence, std::uint64_t nowNs);

    /// The smoothed round trip (an average that gives the newest answer 1/8), nanoseconds; 0 before the first answer.
    [[nodiscard]] std::uint64_t roundTripNs() const { return m_smoothedNs; }
    /// The newest answer's round trip, nanoseconds; 0 before the first.
    [[nodiscard]] std::uint64_t lastRoundTripNs() const { return m_lastNs; }
    /// The fraction, 0 to 1, of the pings of the window that are past their grace and unanswered; 0 before any is.
    [[nodiscard]] float lossFraction() const;
    [[nodiscard]] std::uint32_t pingsSent() const { return m_nextSequence; }
    [[nodiscard]] std::uint32_t pongsReceived() const { return m_answered; }

  private:
    // One remembered ping.
    struct Entry {
        std::uint32_t sequence = 0;
        std::uint64_t sentNs = 0;
        bool used = false;
        bool answered = false;
    };

    std::array<Entry, kWindow> m_entries{};
    std::uint32_t m_nextSequence = 0;
    std::uint32_t m_answered = 0;
    std::uint64_t m_smoothedNs = 0;
    std::uint64_t m_lastNs = 0;
};

} // namespace coney::net
