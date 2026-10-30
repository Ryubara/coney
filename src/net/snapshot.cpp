// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/snapshot.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <span>
#include <utility>
#include <vector>

#include "core/assert.h"
#include "core/error.h"
#include "net/net_id.h"
#include "net/protocol.h"
#include "net/replicated_fields.h"

namespace coney::net {

namespace {

// Every human field's changed bit set: an entry for a human its baseline does not hold.
constexpr std::uint32_t kAllHumanFields = (1U << kHumanFieldCount) - 1U;

// The most bits a Snapshot's header takes: the id byte and four varints of 32 bits at most (5 bytes each).
constexpr std::size_t kMostHeaderBits = 8 + (4 * 5 * 8);

// What no snapshot holds: the baseline of a full snapshot, and the humans before the first is decoded.
const SnapshotHumans& noHumans() {
    static const SnapshotHumans none;
    return none;
}

// A change waiting to be sent: its entry, and the priority it waits with.
struct Candidate {
    SnapshotEntry entry;
    std::uint64_t priority = 0;
};

} // namespace

HumanWords humanWords(std::span<const FieldValue, kHumanFieldCount> values) {
    const auto fields = fieldTable(ReplicatedKind::Human);
    HumanWords words{};
    for (std::size_t field = 0; field < kHumanFieldCount; ++field) {
        words.at(field) = fieldWord(fields[field], values[field]);
    }
    return words;
}

std::array<FieldValue, kHumanFieldCount> humanValues(const HumanWords& words) {
    const auto fields = fieldTable(ReplicatedKind::Human);
    std::array<FieldValue, kHumanFieldCount> values{};
    for (std::size_t field = 0; field < kHumanFieldCount; ++field) {
        const auto value = fieldValue(fields[field], words.at(field));
        CONEY_ASSERT(value.has_value());
        values.at(field) = *value;
    }
    return values;
}

SnapshotSender::SnapshotSender(std::size_t budgetBytes) : m_budgetBytes(budgetBytes) {
    // Room for the header and one human in full, or a new human might never fit.
    const std::size_t fullHuman = kNetIdBits + 1 + kHumanFieldCount + fieldsBits(fieldTable(ReplicatedKind::Human));
    CONEY_ASSERT(budgetBytes * 8 >= kMostHeaderBits + fullHuman);
}

Snapshot SnapshotSender::build(std::uint32_t step, std::span<const SnapshotHuman> humans) {
    CONEY_ASSERT(m_sequence < 0xffffffffU); // four and a half years of snapshots at 30 a second
    Snapshot snapshot;
    snapshot.sequence = ++m_sequence;
    snapshot.step = step;

    // The baseline: the newest acknowledged snapshot, while it is kept. Copied before this snapshot takes the place
    // of the one kSnapshotHistory back, which may be it.
    const Sent& acknowledged = m_sent.at(m_acknowledged % kSnapshotHistory);
    const bool kept = m_acknowledged != 0 && acknowledged.sequence == m_acknowledged &&
                      m_sequence - m_acknowledged <= kSnapshotHistory;
    snapshot.baseline = kept ? m_acknowledged : 0;
    SnapshotHumans holding = kept ? acknowledged.holding : noHumans();

    // This step's humans by id, so they are walked in id order whatever order the game gave them in.
    std::map<NetId, const SnapshotHuman*> now;
    for (const SnapshotHuman& human : humans) {
        CONEY_ASSERT(human.id.valid());
        const bool added = now.emplace(human.id, &human).second;
        CONEY_ASSERT(added); // one entry a human
    }

    // Removals: a human the baseline holds that is not here any more.
    std::vector<Candidate> removals;
    for (const auto& [id, words] : holding) {
        if (!now.contains(id)) {
            removals.push_back(
                Candidate{.entry = SnapshotEntry{.id = id, .removed = true, .changed = 0, .words = {}}, .priority = 0});
        }
    }

    // Changes against the baseline, each human's priority grown by its own rate; a human with nothing to send has
    // none waiting, and a human gone takes its priority with it.
    std::vector<Candidate> changes;
    std::map<NetId, std::uint64_t> priorities;
    for (const auto& [id, human] : now) {
        SnapshotEntry entry{.id = id, .removed = false, .changed = 0, .words = {}};
        const auto held = holding.find(id);
        for (std::size_t field = 0; field < kHumanFieldCount; ++field) {
            if (held == holding.end() || held->second.at(field) != human->words.at(field)) {
                entry.changed |= 1U << field;
                entry.words.at(field) = human->words.at(field);
            }
        }
        if (entry.changed == 0) {
            continue;
        }
        const auto before = m_priority.find(id);
        const std::uint64_t priority = (before == m_priority.end() ? 0 : before->second) + human->priority;
        priorities.emplace(id, priority);
        changes.push_back(Candidate{.entry = entry, .priority = priority});
    }
    // The highest priority first; equal ones in id order, so the order is the same on every machine.
    std::ranges::stable_sort(changes, [](const Candidate& a, const Candidate& b) { return a.priority > b.priority; });

    // Fill the budget: removals first (each a few bytes, and a client must not keep a human that has gone), then the
    // changes by priority; one that does not fit waits, and a smaller one after it may still go.
    const std::size_t budgetBits = m_budgetBytes * 8;
    std::size_t usedBits = snapshotHeaderBits(snapshot, removals.size() + changes.size());
    std::vector<SnapshotEntry> taken;
    const auto offer = [&](const Candidate& candidate) {
        const std::size_t bits = snapshotEntryBits(candidate.entry);
        if (usedBits + bits > budgetBits) {
            return;
        }
        usedBits += bits;
        taken.push_back(candidate.entry);
        if (candidate.entry.removed) {
            holding.erase(candidate.entry.id);
        } else {
            holding.insert_or_assign(candidate.entry.id, now.at(candidate.entry.id)->words);
            priorities.erase(candidate.entry.id); // sent: it starts waiting again from 0
        }
    };
    std::ranges::for_each(removals, offer);
    std::ranges::for_each(changes, offer);

    std::ranges::sort(taken, [](const SnapshotEntry& a, const SnapshotEntry& b) { return a.id < b.id; });
    snapshot.humans = std::move(taken);
    m_priority = std::move(priorities);
    m_sent.at(snapshot.sequence % kSnapshotHistory) =
        Sent{.sequence = snapshot.sequence, .holding = std::move(holding)};
    return snapshot;
}

std::expected<bool, Error> SnapshotSender::acknowledge(std::uint32_t sequence) {
    if (sequence == 0 || sequence > m_sequence) {
        return fail(ErrorCode::Invalid, "network snapshot acknowledged that was never sent");
    }
    if (sequence <= m_acknowledged || m_sent.at(sequence % kSnapshotHistory).sequence != sequence) {
        return false; // late, repeated, or older than the snapshots kept
    }
    m_acknowledged = sequence;
    return true;
}

const SnapshotHumans* SnapshotSender::holding(std::uint32_t sequence) const {
    const Sent& sent = m_sent.at(sequence % kSnapshotHistory);
    return sequence != 0 && sent.sequence == sequence ? &sent.holding : nullptr;
}

std::uint64_t SnapshotSender::priority(NetId id) const {
    const auto found = m_priority.find(id);
    return found == m_priority.end() ? 0 : found->second;
}

std::expected<SnapshotApplied, Error> SnapshotReceiver::apply(const Snapshot& snapshot) {
    if (snapshot.sequence <= m_newest) {
        return SnapshotApplied::Stale;
    }
    const SnapshotHumans* baseline = snapshot.baseline == 0 ? &noHumans() : holding(snapshot.baseline);
    if (baseline == nullptr) {
        return SnapshotApplied::NoBaseline;
    }
    // Copied before this snapshot takes the place of the one kSnapshotHistory back, which may be the baseline.
    SnapshotHumans humans = *baseline;
    for (const SnapshotEntry& entry : snapshot.humans) {
        if (entry.removed) {
            if (humans.erase(entry.id) == 0) {
                return fail(ErrorCode::Invalid, "network snapshot removes a human its baseline does not hold");
            }
            continue;
        }
        const auto held = humans.find(entry.id);
        if (held == humans.end()) {
            if (entry.changed != kAllHumanFields) {
                return fail(ErrorCode::Invalid, "network snapshot adds a human with fields left out");
            }
            humans.emplace(entry.id, entry.words);
            continue;
        }
        // One encoding per snapshot: a field is written only when it differs from the baseline.
        for (std::size_t field = 0; field < kHumanFieldCount; ++field) {
            if ((entry.changed & (1U << field)) == 0) {
                continue;
            }
            if (held->second.at(field) == entry.words.at(field)) {
                return fail(ErrorCode::Invalid, "network snapshot changes a field to the value it has");
            }
            held->second.at(field) = entry.words.at(field);
        }
    }
    m_received.at(snapshot.sequence % kSnapshotHistory) =
        Received{.sequence = snapshot.sequence, .holding = std::move(humans)};
    m_newest = snapshot.sequence;
    m_newestStep = snapshot.step;
    return SnapshotApplied::Applied;
}

const SnapshotHumans& SnapshotReceiver::humans() const {
    const SnapshotHumans* newest = holding(m_newest);
    return newest == nullptr ? noHumans() : *newest;
}

const SnapshotHumans* SnapshotReceiver::holding(std::uint32_t sequence) const {
    const Received& received = m_received.at(sequence % kSnapshotHistory);
    return sequence != 0 && received.sequence == sequence ? &received.holding : nullptr;
}

} // namespace coney::net
