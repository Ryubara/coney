// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/state_digest.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <format>
#include <map>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "core/assert.h"

namespace coney::net {

namespace {

constexpr std::uint64_t kFnvPrime = 0x100000001b3ULL;
constexpr std::string_view kDigestWord = "digest";
constexpr std::size_t kDigestHexDigits = 16;

// `id` as SLOT.GEN for a log.
std::string idText(NetId id) { return std::format("{}.{}", id.slot, id.generation); }

// The words of `text` split at single spaces; an empty word (two spaces, a space at an end) stays as one, so the
// strict reader refuses it.
std::vector<std::string_view> splitWords(std::string_view text) {
    std::vector<std::string_view> words;
    std::size_t start = 0;
    while (true) {
        const std::size_t space = text.find(' ', start);
        words.push_back(text.substr(start, space == std::string_view::npos ? std::string_view::npos : space - start));
        if (space == std::string_view::npos) {
            return words;
        }
        start = space + 1;
    }
}

// A decimal number that fits 32 bits, digits only.
std::optional<std::uint32_t> readDecimal(std::string_view word) {
    if (word.empty() || !std::ranges::all_of(word, [](char c) { return c >= '0' && c <= '9'; })) {
        return std::nullopt;
    }
    std::uint32_t value = 0;
    const auto [end, error] = std::from_chars(word.data(), word.data() + word.size(), value);
    if (error != std::errc{} || end != word.data() + word.size()) {
        return std::nullopt;
    }
    return value;
}

// A digest: exactly kDigestHexDigits lowercase hex digits.
std::optional<std::uint64_t> readDigest(std::string_view word) {
    const auto lowerHex = [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); };
    if (word.size() != kDigestHexDigits || !std::ranges::all_of(word, lowerHex)) {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    std::from_chars(word.data(), word.data() + word.size(), value, 16);
    return value;
}

} // namespace

void DigestHasher::add(std::uint64_t value, std::size_t bytes) {
    CONEY_ASSERT(bytes >= 1 && bytes <= 8);
    for (std::size_t i = 0; i < bytes; ++i) {
        m_hash ^= (value >> (8 * i)) & 0xffU;
        m_hash *= kFnvPrime;
    }
}

std::uint64_t humansDigest(const SnapshotHumans& humans) {
    DigestHasher hasher;
    hasher.add(humans.size(), 4);
    for (const auto& [id, words] : humans) { // a map: id order on every machine
        hasher.add(id.slot, 2);
        hasher.add(id.generation, 1);
        for (const std::uint32_t word : words) {
            hasher.add(word, 4);
        }
    }
    return hasher.value();
}

std::uint64_t padsDigest(const PortSamples& pads) {
    DigestHasher hasher;
    for (const PadSample& pad : pads) {
        hasher.add(pad.connected ? 1U : 0U, 1);
        if (!pad.connected) {
            continue; // the game ignores the rest of an unplugged pad
        }
        hasher.add(pad.buttons, 2);
        for (const std::uint8_t stick : pad.sticks) {
            hasher.add(stick, 1);
        }
        for (const std::uint8_t pressure : pad.pressure) {
            hasher.add(pressure, 1);
        }
    }
    return hasher.value();
}

std::optional<HumanDifference> firstDifference(const SnapshotHumans& first, const SnapshotHumans& second) {
    // Walk both maps in id order together: the first id either holds alone, or the first field of a shared id.
    auto a = first.begin();
    auto b = second.begin();
    while (a != first.end() || b != second.end()) {
        if (b == second.end() || (a != first.end() && a->first < b->first)) {
            return HumanDifference{.id = a->first, .field = std::nullopt};
        }
        if (a == first.end() || b->first < a->first) {
            return HumanDifference{.id = b->first, .field = std::nullopt};
        }
        for (std::size_t i = 0; i < kHumanFieldCount; ++i) {
            if (a->second.at(i) != b->second.at(i)) {
                return HumanDifference{.id = a->first, .field = static_cast<HumanField>(i)};
            }
        }
        ++a;
        ++b;
    }
    return std::nullopt;
}

std::string describeDifference(const HumanDifference& difference, const SnapshotHumans& first,
                               const SnapshotHumans& second) {
    if (!difference.field.has_value()) {
        const bool inFirst = first.contains(difference.id);
        return std::format("human {}: held by the {} only", idText(difference.id), inFirst ? "first" : "second");
    }
    const auto index = static_cast<std::size_t>(*difference.field);
    const auto firstWords = first.find(difference.id);
    const auto secondWords = second.find(difference.id);
    CONEY_ASSERT(firstWords != first.end() && secondWords != second.end());
    return std::format("human {} {}: {} against {}", idText(difference.id),
                       fieldTable(ReplicatedKind::Human)[index].name, firstWords->second.at(index),
                       secondWords->second.at(index));
}

std::string formatDigestLine(const DigestLine& line) {
    std::string text = std::format("{} {} {:016x} {}", kDigestWord, line.step, line.humans, line.humanCount);
    if (line.pads.has_value()) {
        text += std::format(" {:016x}", *line.pads);
    }
    return text;
}

std::expected<DigestLine, Error> parseDigestLine(std::string_view text) {
    const auto invalid = [text] {
        return std::unexpected(Error{ErrorCode::Invalid, std::format("not a digest line: '{}'", text)});
    };
    const std::vector<std::string_view> words = splitWords(text);
    if ((words.size() != 4 && words.size() != 5) || words[0] != kDigestWord) {
        return invalid();
    }
    const auto step = readDecimal(words[1]);
    const auto humans = readDigest(words[2]);
    const auto count = readDecimal(words[3]);
    if (!step || !humans || !count) {
        return invalid();
    }
    DigestLine line{.step = *step, .humans = *humans, .humanCount = *count, .pads = std::nullopt};
    if (words.size() == 5) {
        const auto pads = readDigest(words[4]);
        if (!pads) {
            return invalid();
        }
        line.pads = pads;
    }
    return line;
}

DigestMatch matchDigests(std::span<const DigestLine> host, std::span<const DigestLine> guest) {
    std::map<std::uint32_t, const DigestLine*> byStep;
    for (const DigestLine& line : host) {
        byStep.emplace(line.step, &line);
    }
    DigestMatch match;
    for (const DigestLine& line : guest) {
        const auto found = byStep.find(line.step);
        if (found == byStep.end()) {
            match.firstUnknown = match.firstUnknown.value_or(line.step);
            continue;
        }
        if (found->second->humans == line.humans && found->second->humanCount == line.humanCount) {
            ++match.matched;
        } else if (!match.firstMismatch.has_value()) {
            match.firstMismatch = line.step;
        }
    }
    return match;
}

} // namespace coney::net
