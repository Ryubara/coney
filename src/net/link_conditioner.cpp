// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/link_conditioner.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "core/assert.h"
#include "core/error.h"
#include "net/transport.h"

namespace coney::net {

namespace {

constexpr std::uint64_t kNsPerMs = 1'000'000;

} // namespace

LinkConditioner::LinkConditioner(Transport& inner, std::uint32_t seed)
    : m_inner(inner), m_state(seed == 0 ? 1 : seed) {}

float LinkConditioner::draw() {
    // xorshift32 (Marsaglia): small, fast and the same on every platform.
    m_state ^= m_state << 13U;
    m_state ^= m_state >> 17U;
    m_state ^= m_state << 5U;
    // The top 24 bits as a fraction, so the result is exactly representable and below 1.
    return static_cast<float>(m_state >> 8U) / static_cast<float>(1U << 24U);
}

std::uint64_t LinkConditioner::delayNs() {
    std::uint64_t delay = std::uint64_t{m_conditions.latencyMs} * kNsPerMs;
    if (m_conditions.jitterMs > 0) {
        delay += static_cast<std::uint64_t>(draw() * static_cast<float>(m_conditions.jitterMs) *
                                            static_cast<float>(kNsPerMs));
    }
    return delay;
}

std::expected<void, Error> LinkConditioner::send(PeerId peer, Delivery delivery, std::span<const std::byte> data) {
    CONEY_ASSERT(!data.empty());
    const auto found = m_peers.find(peer);
    if (found == m_peers.end() || found->second.closing) {
        return fail(ErrorCode::NotFound, "connection is not up");
    }
    PeerState& state = found->second;
    const std::vector<std::byte> bytes(data.begin(), data.end());

    if (delivery == Delivery::ReliableOrdered) {
        // Delayed only, and never before the reliable packet ahead of it.
        const std::uint64_t release = std::max(m_nowNs + delayNs(), state.lastReliableReleaseNs);
        state.lastReliableReleaseNs = release;
        m_held.push_back(
            Held{.releaseNs = release, .order = m_nextOrder++, .peer = peer, .delivery = delivery, .data = bytes});
        return {};
    }

    // Unreliable: lost, or sent once or twice, each copy with its own delay and maybe held back to reorder it.
    if (draw() < m_conditions.lossFraction) {
        ++m_dropped;
        return {};
    }
    const int copies = draw() < m_conditions.duplicateFraction ? 2 : 1;
    const std::uint64_t order = m_nextOrder++; // the copies share it: the second is the same packet again
    for (int i = 0; i < copies; ++i) {
        std::uint64_t release = m_nowNs + delayNs();
        if (draw() < m_conditions.reorderFraction) {
            release += std::uint64_t{m_conditions.reorderDelayMs} * kNsPerMs;
        }
        m_held.push_back(Held{.releaseNs = release, .order = order, .peer = peer, .delivery = delivery, .data = bytes});
    }
    return {};
}

void LinkConditioner::advance(std::uint64_t elapsedNs) {
    m_nowNs += elapsedNs;
    m_inner.advance(elapsedNs);
    release();
}

std::optional<TransportEvent> LinkConditioner::poll() {
    std::optional<TransportEvent> event = m_inner.poll();
    if (event && event->kind == TransportEventKind::Connected) {
        m_peers[event->peer] = PeerState{.lastReliableReleaseNs = m_nowNs, .lastSequencedOrder = 0, .closing = false};
    } else if (event && event->kind == TransportEventKind::Disconnected) {
        // Gone underneath: whatever is still held for it can never be sent.
        m_peers.erase(event->peer);
        std::erase_if(m_held, [&](const Held& held) { return held.peer == event->peer; });
    }
    return event;
}

void LinkConditioner::flush() {
    release();
    m_inner.flush();
}

void LinkConditioner::disconnect(PeerId peer) {
    const auto found = m_peers.find(peer);
    if (found != m_peers.end()) {
        found->second.closing = true;
        release();
    }
}

void LinkConditioner::release() {
    // Due packets in the order they would arrive: release time, then send order.
    std::vector<Held> due;
    std::erase_if(m_held, [&](Held& held) {
        if (held.releaseNs > m_nowNs) {
            return false;
        }
        due.push_back(std::move(held));
        return true;
    });
    std::ranges::sort(due, [](const Held& a, const Held& b) {
        return a.releaseNs != b.releaseNs ? a.releaseNs < b.releaseNs : a.order < b.order;
    });
    for (Held& held : due) {
        const auto peer = m_peers.find(held.peer);
        if (peer == m_peers.end()) {
            continue;
        }
        if (held.delivery == Delivery::UnreliableSequenced) {
            // A sequenced packet that would arrive after a newer one is dropped, as the contract says.
            if (held.order <= peer->second.lastSequencedOrder) {
                ++m_dropped;
                continue;
            }
            peer->second.lastSequencedOrder = held.order;
        }
        if (auto sent = m_inner.send(held.peer, held.delivery, held.data); !sent) {
            continue; // the connection went underneath; poll() will report it
        }
    }

    // A closing connection goes once no reliable packet of it is held; its unreliable leftovers go with it.
    for (auto it = m_peers.begin(); it != m_peers.end();) {
        const PeerId id = it->first;
        const bool reliableHeld = std::ranges::any_of(
            m_held, [&](const Held& held) { return held.peer == id && held.delivery == Delivery::ReliableOrdered; });
        if (it->second.closing && !reliableHeld) {
            std::erase_if(m_held, [&](const Held& held) { return held.peer == id; });
            it = m_peers.erase(it);
            m_inner.disconnect(id);
        } else {
            ++it;
        }
    }
}

} // namespace coney::net
