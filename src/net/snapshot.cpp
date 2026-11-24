// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/snapshot.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "core/assert.h"
#include "core/error.h"
#include "net/net_id.h"
#include "net/protocol.h"
#include "net/replicated_fields.h"

namespace coney::net {

namespace {

// The most bits a Snapshot's header takes: the id byte and four varints of 32 bits at most (5 bytes each), the
// counts of HUD entries, HUD elements and HUD texts (one byte each: at most kMaxPlayerSlots and kHudElements) and the
// radar blips' (two bytes: at most kMaxRadarBlips).
constexpr std::size_t kMostHeaderBits = 8 + (4 * 5 * 8) + (3 * 8) + 16;
static_assert(kHudElements < 0x80, "a HUD element or text count is a one-byte varint");

// The bits of a HUD text of kMaxHudTextBytes: its key, its removed bit, its length (a two-byte varint) and its bytes.
constexpr std::size_t kLongestHudTextBits = kHudElementBits + 1 + 16 + (kMaxHudTextBytes * 8);

// What no snapshot holds: the baseline of a full snapshot, and the humans before the first is decoded.
const SnapshotHumans& noHumans() {
    static const SnapshotHumans none;
    return none;
}

// What no snapshot holds of the players' HUDs.
const SnapshotHuds& noHuds() {
    static const SnapshotHuds none;
    return none;
}

// What no snapshot holds of the HUD's elements and texts.
const SnapshotHudElements& noHudElements() {
    static const SnapshotHudElements none;
    return none;
}

// What no snapshot holds of the HUD's texts.
const SnapshotHudTexts& noHudTexts() {
    static const SnapshotHudTexts none;
    return none;
}

// What no snapshot holds of the radar's blips.
const SnapshotBlips& noBlips() {
    static const SnapshotBlips none;
    return none;
}

// The bits of a blip in full: what the sender keeps room for, in the blips' share and in the packet.
std::size_t fullBlipBits() {
    const auto fields = fieldTable(ReplicatedKind::HudBlip);
    return kRadarBlipSlotBits + 1 + fields.size() + fieldsBits(fields);
}

// The bits of every HUD element in full: what the sender keeps room for, as for the players' HUDs.
std::size_t allHudElementsBits() {
    std::size_t bits = 0;
    for (std::size_t key = 0; key < kHudElements; ++key) {
        const auto fields = fieldTable(hudElementKind(static_cast<std::uint8_t>(key)));
        bits += kHudElementBits + 1 + fields.size() + fieldsBits(fields);
    }
    return bits;
}

// The HUD elements' entries a snapshot against `held` needs for `now` (by key): a removal for each element held and
// not here, else the fields changed (every field for one not held).
std::vector<SnapshotHudElementEntry> hudElementChanges(const SnapshotHudElements& held,
                                                       const std::map<std::uint8_t, const SnapshotHudElement*>& now) {
    std::vector<SnapshotHudElementEntry> changes;
    for (const auto& [key, words] : held) {
        if (!now.contains(key)) {
            changes.push_back(SnapshotHudElementEntry{.key = key, .removed = true, .changed = 0, .words = {}});
        }
    }
    for (const auto& [key, element] : now) {
        SnapshotHudElementEntry entry{.key = key, .removed = false, .changed = 0, .words = {}};
        const auto before = held.find(key);
        for (std::size_t field = 0; field < hudElementFieldCount(key); ++field) {
            if (before == held.end() || before->second.at(field) != element->words.at(field)) {
                entry.changed |= 1U << field;
                entry.words.at(field) = element->words.at(field);
            }
        }
        if (entry.changed != 0) {
            changes.push_back(entry);
        }
    }
    std::ranges::sort(changes, {}, &SnapshotHudElementEntry::key);
    return changes;
}

// The HUD texts' entries a snapshot against `held` needs for `now` (by key): a removal for each text held and not
// here, else each text not held as it is.
std::vector<SnapshotHudTextEntry> hudTextChanges(const SnapshotHudTexts& held,
                                                 const std::map<std::uint8_t, const SnapshotHudText*>& now) {
    std::vector<SnapshotHudTextEntry> changes;
    for (const auto& [key, text] : held) {
        if (!now.contains(key)) {
            changes.push_back(SnapshotHudTextEntry{.key = key, .removed = true, .text = {}});
        }
    }
    for (const auto& [key, text] : now) {
        const auto before = held.find(key);
        if (before == held.end() || before->second != text->text) {
            changes.push_back(SnapshotHudTextEntry{.key = key, .removed = false, .text = text->text});
        }
    }
    std::ranges::sort(changes, {}, &SnapshotHudTextEntry::key);
    return changes;
}

// A change waiting to be sent: its entry, and the priority it waits with.
struct Candidate {
    SnapshotEntry entry;
    std::uint64_t priority = 0;
};

// A radar blip's change waiting to be sent: its entry, and the priority it waits with.
struct BlipCandidate {
    SnapshotBlipEntry entry;
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

HudWords hudWords(std::span<const FieldValue, kHudFieldCount> values) {
    const auto fields = fieldTable(ReplicatedKind::PlayerHud);
    HudWords words{};
    for (std::size_t field = 0; field < kHudFieldCount; ++field) {
        words.at(field) = fieldWord(fields[field], values[field]);
    }
    return words;
}

std::array<FieldValue, kHudFieldCount> hudValues(const HudWords& words) {
    const auto fields = fieldTable(ReplicatedKind::PlayerHud);
    std::array<FieldValue, kHudFieldCount> values{};
    for (std::size_t field = 0; field < kHudFieldCount; ++field) {
        const auto value = fieldValue(fields[field], words.at(field));
        CONEY_ASSERT(value.has_value());
        values.at(field) = *value;
    }
    return values;
}

SnapshotSender::SnapshotSender(std::size_t budgetBytes) : m_budgetBytes(budgetBytes) {
    // Room for the header, every player slot's HUD and one human in full, or a new HUD or human might never fit.
    const std::size_t fullHuman = kNetIdBits + 1 + kHumanFieldCount + fieldsBits(fieldTable(ReplicatedKind::Human));
    const std::size_t fullHud =
        kPlayerSlotBits + 1 + kHudFieldCount + fieldsBits(fieldTable(ReplicatedKind::PlayerHud));
    // The HUD's standing state too: every element in full and one text of the longest, or a long text might never fit;
    // and the blips' whole share, which goes before the humans, or the blips could leave no room for a human.
    CONEY_ASSERT(budgetBytes * 8 >= kMostHeaderBits + (kMaxPlayerSlots * fullHud) + allHudElementsBits() +
                                        kLongestHudTextBits + (budgetBytes * 8 / kSnapshotBlipShare) + fullHuman);
    // The radar's blips have a share of the packet, which must hold one in full or a new blip might never fit.
    CONEY_ASSERT(budgetBytes * 8 / kSnapshotBlipShare >= fullBlipBits());
}

Snapshot SnapshotSender::build(std::uint32_t step, std::span<const SnapshotHuman> humans,
                               std::span<const SnapshotHud> huds, std::span<const SnapshotHudElement> elements,
                               std::span<const SnapshotHudText> texts, std::span<const SnapshotBlip> blips) {
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
    SnapshotHuds hudsHeld = kept ? acknowledged.huds : noHuds();
    SnapshotHudElements elementsHeld = kept ? acknowledged.elements : noHudElements();
    SnapshotHudTexts textsHeld = kept ? acknowledged.texts : noHudTexts();
    SnapshotBlips blipsHeld = kept ? acknowledged.blips : noBlips();

    // This step's humans by id, so they are walked in id order whatever order the game gave them in.
    std::map<NetId, const SnapshotHuman*> now;
    for (const SnapshotHuman& human : humans) {
        CONEY_ASSERT(human.id.valid());
        const bool added = now.emplace(human.id, &human).second;
        CONEY_ASSERT(added); // one entry a human
    }

    // The players' HUDs by slot: a removal for a slot the baseline holds that is not here, else the fields changed.
    std::map<std::uint8_t, const SnapshotHud*> hudsNow;
    for (const SnapshotHud& hud : huds) {
        CONEY_ASSERT(hud.slot < kMaxPlayerSlots);
        const bool added = hudsNow.emplace(hud.slot, &hud).second;
        CONEY_ASSERT(added); // one entry a slot
    }
    std::vector<SnapshotHudEntry> hudChanges;
    for (const auto& [slot, words] : hudsHeld) {
        if (!hudsNow.contains(slot)) {
            hudChanges.push_back(SnapshotHudEntry{.slot = slot, .removed = true, .changed = 0, .words = {}});
        }
    }
    for (const auto& [slot, hud] : hudsNow) {
        SnapshotHudEntry entry{.slot = slot, .removed = false, .changed = 0, .words = {}};
        const auto held = hudsHeld.find(slot);
        for (std::size_t field = 0; field < kHudFieldCount; ++field) {
            if (held == hudsHeld.end() || held->second.at(field) != hud->words.at(field)) {
                entry.changed |= 1U << field;
                entry.words.at(field) = hud->words.at(field);
            }
        }
        if (entry.changed != 0) {
            hudChanges.push_back(entry);
        }
    }

    // The HUD's standing state by key: its elements' changes and its texts', as the HUDs' are.
    std::map<std::uint8_t, const SnapshotHudElement*> elementsNow;
    for (const SnapshotHudElement& element : elements) {
        CONEY_ASSERT(element.key < kHudElements);
        for (std::size_t field = hudElementFieldCount(element.key); field < kMaxHudElementFields; ++field) {
            CONEY_ASSERT(element.words.at(field) == 0); // nothing past its table
        }
        const bool added = elementsNow.emplace(element.key, &element).second;
        CONEY_ASSERT(added); // one entry a key
    }
    std::map<std::uint8_t, const SnapshotHudText*> textsNow;
    for (const SnapshotHudText& text : texts) {
        CONEY_ASSERT(hudElementHasText(text.key));
        CONEY_ASSERT(!text.text.empty() && text.text.size() <= kMaxHudTextBytes);
        const bool added = textsNow.emplace(text.key, &text).second;
        CONEY_ASSERT(added); // one entry a key
    }
    const std::vector<SnapshotHudElementEntry> elementChanges = hudElementChanges(elementsHeld, elementsNow);
    const std::vector<SnapshotHudTextEntry> textChanges = hudTextChanges(textsHeld, textsNow);

    // The radar's blips by slot: a removal for each the baseline holds that is not here; for the rest the fields
    // changed, each blip's priority grown by its own rate while its change waits, as the humans' is.
    std::map<std::uint8_t, const SnapshotBlip*> blipsNow;
    for (const SnapshotBlip& blip : blips) {
        CONEY_ASSERT(blip.slot < kMaxRadarBlips);
        const bool added = blipsNow.emplace(blip.slot, &blip).second;
        CONEY_ASSERT(added); // one entry a slot
    }
    std::vector<SnapshotBlipEntry> blipRemovals;
    for (const auto& [slot, words] : blipsHeld) {
        if (!blipsNow.contains(slot)) {
            blipRemovals.push_back(SnapshotBlipEntry{.slot = slot, .removed = true, .changed = 0, .words = {}});
        }
    }
    std::vector<BlipCandidate> blipChanges;
    std::map<std::uint8_t, std::uint64_t> blipPriorities;
    for (const auto& [slot, blip] : blipsNow) {
        SnapshotBlipEntry entry{.slot = slot, .removed = false, .changed = 0, .words = {}};
        const auto held = blipsHeld.find(slot);
        for (std::size_t field = 0; field < kBlipFieldCount; ++field) {
            if (held == blipsHeld.end() || held->second.at(field) != blip->words.at(field)) {
                entry.changed |= 1U << field;
                entry.words.at(field) = blip->words.at(field);
            }
        }
        if (entry.changed == 0) {
            continue;
        }
        const auto before = m_blipPriority.find(slot);
        const std::uint64_t priority = (before == m_blipPriority.end() ? 0 : before->second) + blip->priority;
        blipPriorities.emplace(slot, priority);
        blipChanges.push_back(BlipCandidate{.entry = entry, .priority = priority});
    }
    // The highest priority first; equal ones in slot order.
    std::ranges::stable_sort(blipChanges,
                             [](const BlipCandidate& a, const BlipCandidate& b) { return a.priority > b.priority; });

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

    // Fill the budget: the HUDs and the HUD's elements first (a handful of small entries, and the constructor keeps
    // room for all of them), then the HUD's texts in key order (seldom changed, and the room left always takes the
    // longest), then removals (each a few bytes, and a client must not keep a human that has gone), then the changes
    // by priority; one that does not fit waits, and a smaller one after it may still go.
    const std::size_t budgetBits = m_budgetBytes * 8;
    std::size_t usedBits =
        snapshotHeaderBits(snapshot, removals.size() + changes.size(), hudChanges.size(), elementChanges.size(),
                           textChanges.size(), blipRemovals.size() + blipChanges.size());
    std::vector<SnapshotHudEntry> hudsTaken;
    for (const SnapshotHudEntry& entry : hudChanges) {
        const std::size_t bits = snapshotHudEntryBits(entry);
        if (usedBits + bits > budgetBits) {
            continue;
        }
        usedBits += bits;
        hudsTaken.push_back(entry);
        if (entry.removed) {
            hudsHeld.erase(entry.slot);
        } else {
            hudsHeld.insert_or_assign(entry.slot, hudsNow.at(entry.slot)->words);
        }
    }
    std::ranges::sort(hudsTaken, [](const SnapshotHudEntry& a, const SnapshotHudEntry& b) { return a.slot < b.slot; });
    snapshot.huds = std::move(hudsTaken);
    for (const SnapshotHudElementEntry& entry : elementChanges) {
        const std::size_t bits = snapshotHudElementEntryBits(entry);
        if (usedBits + bits > budgetBits) {
            continue;
        }
        usedBits += bits;
        snapshot.hudElements.push_back(entry);
        if (entry.removed) {
            elementsHeld.erase(entry.key);
        } else {
            elementsHeld.insert_or_assign(entry.key, elementsNow.at(entry.key)->words);
        }
    }
    for (const SnapshotHudTextEntry& entry : textChanges) {
        const std::size_t bits = snapshotHudTextEntryBits(entry);
        if (usedBits + bits > budgetBits) {
            continue; // it waits for a later snapshot's room
        }
        usedBits += bits;
        snapshot.hudTexts.push_back(entry);
        if (entry.removed) {
            textsHeld.erase(entry.key);
        } else {
            textsHeld.insert_or_assign(entry.key, entry.text);
        }
    }
    // The radar's blips: the removals, each a byte, then the changes by priority, in the blips' share of the budget;
    // one that does not fit waits with its priority grown, and a smaller one after it may still go.
    const std::size_t blipShareBits = budgetBits / kSnapshotBlipShare;
    std::size_t blipBits = 0;
    std::vector<SnapshotBlipEntry> blipsTaken;
    const auto offerBlip = [&](const SnapshotBlipEntry& entry) {
        const std::size_t bits = snapshotBlipEntryBits(entry);
        if (usedBits + bits > budgetBits || blipBits + bits > blipShareBits) {
            return false;
        }
        usedBits += bits;
        blipBits += bits;
        blipsTaken.push_back(entry);
        return true;
    };
    for (const SnapshotBlipEntry& entry : blipRemovals) {
        if (offerBlip(entry)) {
            blipsHeld.erase(entry.slot);
        }
    }
    for (const BlipCandidate& candidate : blipChanges) {
        if (offerBlip(candidate.entry)) {
            blipsHeld.insert_or_assign(candidate.entry.slot, blipsNow.at(candidate.entry.slot)->words);
            blipPriorities.erase(candidate.entry.slot); // sent: it starts waiting again from 0
        }
    }
    std::ranges::sort(blipsTaken, {}, &SnapshotBlipEntry::slot);
    snapshot.blips = std::move(blipsTaken);
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
    m_blipPriority = std::move(blipPriorities);
    m_sent.at(snapshot.sequence % kSnapshotHistory) = Sent{.sequence = snapshot.sequence,
                                                           .holding = std::move(holding),
                                                           .huds = std::move(hudsHeld),
                                                           .elements = std::move(elementsHeld),
                                                           .texts = std::move(textsHeld),
                                                           .blips = std::move(blipsHeld)};
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

const SnapshotHuds* SnapshotSender::holdingHuds(std::uint32_t sequence) const {
    const Sent& sent = m_sent.at(sequence % kSnapshotHistory);
    return sequence != 0 && sent.sequence == sequence ? &sent.huds : nullptr;
}

const SnapshotBlips* SnapshotSender::holdingBlips(std::uint32_t sequence) const {
    const Sent& sent = m_sent.at(sequence % kSnapshotHistory);
    return sequence != 0 && sent.sequence == sequence ? &sent.blips : nullptr;
}

std::uint64_t SnapshotSender::blipPriority(std::uint8_t slot) const {
    const auto found = m_blipPriority.find(slot);
    return found == m_blipPriority.end() ? 0 : found->second;
}

const SnapshotBlips& SnapshotReceiver::blips() const {
    const SnapshotBlips* newest = holdingBlips(m_newest);
    return newest == nullptr ? noBlips() : *newest;
}

const SnapshotBlips* SnapshotReceiver::holdingBlips(std::uint32_t sequence) const {
    const Received& received = m_received.at(sequence % kSnapshotHistory);
    return sequence != 0 && received.sequence == sequence ? &received.blips : nullptr;
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
    const SnapshotHuds* hudsBaseline = snapshot.baseline == 0 ? &noHuds() : holdingHuds(snapshot.baseline);
    CONEY_ASSERT(hudsBaseline != nullptr); // kept with the humans
    // Copied before this snapshot takes the place of the one kSnapshotHistory back, which may be the baseline.
    SnapshotHumans humans = *baseline;
    SnapshotHuds huds = *hudsBaseline;
    const SnapshotHudElements* elementsBaseline =
        snapshot.baseline == 0 ? &noHudElements() : holdingHudElements(snapshot.baseline);
    const SnapshotHudTexts* textsBaseline = snapshot.baseline == 0 ? &noHudTexts() : holdingHudTexts(snapshot.baseline);
    CONEY_ASSERT(elementsBaseline != nullptr && textsBaseline != nullptr); // kept with the humans
    SnapshotHudElements elements = *elementsBaseline;
    SnapshotHudTexts texts = *textsBaseline;
    const SnapshotBlips* blipsBaseline = snapshot.baseline == 0 ? &noBlips() : holdingBlips(snapshot.baseline);
    CONEY_ASSERT(blipsBaseline != nullptr); // kept with the humans
    SnapshotBlips blipsHeld = *blipsBaseline;
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
    // The players' HUDs, by the same rules as the humans.
    for (const SnapshotHudEntry& entry : snapshot.huds) {
        if (entry.removed) {
            if (huds.erase(entry.slot) == 0) {
                return fail(ErrorCode::Invalid, "network snapshot removes a HUD its baseline does not hold");
            }
            continue;
        }
        const auto held = huds.find(entry.slot);
        if (held == huds.end()) {
            if (entry.changed != kAllHudFields) {
                return fail(ErrorCode::Invalid, "network snapshot adds a HUD with fields left out");
            }
            huds.emplace(entry.slot, entry.words);
            continue;
        }
        for (std::size_t field = 0; field < kHudFieldCount; ++field) {
            if ((entry.changed & (1U << field)) == 0) {
                continue;
            }
            if (held->second.at(field) == entry.words.at(field)) {
                return fail(ErrorCode::Invalid, "network snapshot changes a HUD field to the value it has");
            }
            held->second.at(field) = entry.words.at(field);
        }
    }
    // The HUD's elements by the same rules, and its texts: a removal of one not held, or a text "changed" to the text
    // held, is refused.
    for (const SnapshotHudElementEntry& entry : snapshot.hudElements) {
        if (entry.removed) {
            if (elements.erase(entry.key) == 0) {
                return fail(ErrorCode::Invalid, "network snapshot removes a HUD element its baseline does not hold");
            }
            continue;
        }
        const auto held = elements.find(entry.key);
        if (held == elements.end()) {
            if (entry.changed != allHudElementFields(entry.key)) {
                return fail(ErrorCode::Invalid, "network snapshot adds a HUD element with fields left out");
            }
            elements.emplace(entry.key, entry.words);
            continue;
        }
        for (std::size_t field = 0; field < hudElementFieldCount(entry.key); ++field) {
            if ((entry.changed & (1U << field)) == 0) {
                continue;
            }
            if (held->second.at(field) == entry.words.at(field)) {
                return fail(ErrorCode::Invalid, "network snapshot changes a HUD element's field to the value it has");
            }
            held->second.at(field) = entry.words.at(field);
        }
    }
    for (const SnapshotHudTextEntry& entry : snapshot.hudTexts) {
        if (entry.removed) {
            if (texts.erase(entry.key) == 0) {
                return fail(ErrorCode::Invalid, "network snapshot removes a HUD text its baseline does not hold");
            }
            continue;
        }
        const auto held = texts.find(entry.key);
        if (held != texts.end() && held->second == entry.text) {
            return fail(ErrorCode::Invalid, "network snapshot changes a HUD text to the text it has");
        }
        texts.insert_or_assign(entry.key, entry.text);
    }
    // The radar's blips, by the same rules as the humans, and each blip's words must keep a blip's own.
    for (const SnapshotBlipEntry& entry : snapshot.blips) {
        if (entry.removed) {
            if (blipsHeld.erase(entry.slot) == 0) {
                return fail(ErrorCode::Invalid, "network snapshot removes a radar blip its baseline does not hold");
            }
            continue;
        }
        const auto held = blipsHeld.find(entry.slot);
        if (held == blipsHeld.end()) {
            if (entry.changed != kAllBlipFields) {
                return fail(ErrorCode::Invalid, "network snapshot adds a radar blip with fields left out");
            }
            if (const char* broken = blipWordsBroken(entry.words)) {
                return fail(ErrorCode::Invalid, broken);
            }
            blipsHeld.emplace(entry.slot, entry.words);
            continue;
        }
        BlipWords words = held->second;
        for (std::size_t field = 0; field < kBlipFieldCount; ++field) {
            if ((entry.changed & (1U << field)) == 0) {
                continue;
            }
            if (words.at(field) == entry.words.at(field)) {
                return fail(ErrorCode::Invalid, "network snapshot changes a radar blip's field to the value it has");
            }
            words.at(field) = entry.words.at(field);
        }
        if (const char* broken = blipWordsBroken(words)) {
            return fail(ErrorCode::Invalid, broken);
        }
        held->second = words;
    }
    m_received.at(snapshot.sequence % kSnapshotHistory) = Received{.sequence = snapshot.sequence,
                                                                   .step = snapshot.step,
                                                                   .holding = std::move(humans),
                                                                   .huds = std::move(huds),
                                                                   .elements = std::move(elements),
                                                                   .texts = std::move(texts),
                                                                   .blips = std::move(blipsHeld)};
    m_newest = snapshot.sequence;
    m_newestStep = snapshot.step;
    return SnapshotApplied::Applied;
}

const SnapshotHumans& SnapshotReceiver::humans() const {
    const SnapshotHumans* newest = holding(m_newest);
    return newest == nullptr ? noHumans() : *newest;
}

const SnapshotHuds& SnapshotReceiver::huds() const {
    const SnapshotHuds* newest = holdingHuds(m_newest);
    return newest == nullptr ? noHuds() : *newest;
}

const SnapshotHuds* SnapshotReceiver::holdingHuds(std::uint32_t sequence) const {
    const Received& received = m_received.at(sequence % kSnapshotHistory);
    return sequence != 0 && received.sequence == sequence ? &received.huds : nullptr;
}

const SnapshotHudElements* SnapshotSender::holdingHudElements(std::uint32_t sequence) const {
    const Sent& sent = m_sent.at(sequence % kSnapshotHistory);
    return sequence != 0 && sent.sequence == sequence ? &sent.elements : nullptr;
}

const SnapshotHudTexts* SnapshotSender::holdingHudTexts(std::uint32_t sequence) const {
    const Sent& sent = m_sent.at(sequence % kSnapshotHistory);
    return sequence != 0 && sent.sequence == sequence ? &sent.texts : nullptr;
}

const SnapshotHudElements& SnapshotReceiver::hudElements() const {
    const SnapshotHudElements* newest = holdingHudElements(m_newest);
    return newest == nullptr ? noHudElements() : *newest;
}

const SnapshotHudElements* SnapshotReceiver::holdingHudElements(std::uint32_t sequence) const {
    const Received& received = m_received.at(sequence % kSnapshotHistory);
    return sequence != 0 && received.sequence == sequence ? &received.elements : nullptr;
}

const SnapshotHudTexts& SnapshotReceiver::hudTexts() const {
    const SnapshotHudTexts* newest = holdingHudTexts(m_newest);
    return newest == nullptr ? noHudTexts() : *newest;
}

const SnapshotHudTexts* SnapshotReceiver::holdingHudTexts(std::uint32_t sequence) const {
    const Received& received = m_received.at(sequence % kSnapshotHistory);
    return sequence != 0 && received.sequence == sequence ? &received.texts : nullptr;
}

const SnapshotHumans* SnapshotReceiver::holding(std::uint32_t sequence) const {
    const Received& received = m_received.at(sequence % kSnapshotHistory);
    return sequence != 0 && received.sequence == sequence ? &received.holding : nullptr;
}

std::optional<std::uint32_t> SnapshotReceiver::heldStep(std::uint32_t sequence) const {
    const Received& received = m_received.at(sequence % kSnapshotHistory);
    if (sequence == 0 || received.sequence != sequence) {
        return std::nullopt;
    }
    return received.step;
}

} // namespace coney::net
