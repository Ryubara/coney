// SPDX-License-Identifier: GPL-3.0-or-later
// Stubs until snapshots (#554): they compile the tests and do nothing.
#include "net/snapshot.h"

namespace coney::net {

HumanWords humanWords(std::span<const FieldValue, kHumanFieldCount> /*values*/) { return {}; }

std::array<FieldValue, kHumanFieldCount> humanValues(const HumanWords& /*words*/) { return {}; }

SnapshotSender::SnapshotSender(std::size_t budgetBytes) : m_budgetBytes(budgetBytes) {}

Snapshot SnapshotSender::build(std::uint32_t step, std::span<const SnapshotHuman> /*humans*/) {
    return Snapshot{.sequence = ++m_sequence, .baseline = 0, .step = step, .humans = {}};
}

std::expected<bool, Error> SnapshotSender::acknowledge(std::uint32_t /*sequence*/) { return false; }

const SnapshotHumans* SnapshotSender::holding(std::uint32_t /*sequence*/) const { return nullptr; }

std::uint64_t SnapshotSender::priority(NetId /*id*/) const { return 0; }

std::expected<SnapshotApplied, Error> SnapshotReceiver::apply(const Snapshot& /*snapshot*/) {
    return SnapshotApplied::Stale;
}

const SnapshotHumans& SnapshotReceiver::humans() const {
    static const SnapshotHumans none;
    return none;
}

const SnapshotHumans* SnapshotReceiver::holding(std::uint32_t /*sequence*/) const { return nullptr; }

} // namespace coney::net
