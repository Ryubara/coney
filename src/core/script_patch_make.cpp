// SPDX-License-Identifier: GPL-3.0-or-later
// Making script patches: makePatch() of core/script_patch_apply.h.
#include "core/script_patch_apply.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/script_patch.h"
#include "core/sequence_diff.h"

namespace coney::patch {

namespace {

// The steps of makePatch(): the diff read as operations, with copies and built lines in place of the base's text.
class Maker {
  public:
    Maker(std::span<const std::string_view> base, std::span<const std::string_view> edited)
        : m_base(base), m_edited(edited) {}

    // The operations from the base to the edited copy.
    std::vector<Operation> run() {
        // Lines become numbers, so the diff compares numbers; equal lines share one.
        std::vector<std::uint32_t> a;
        std::vector<std::uint32_t> b;
        for (const std::string_view line : m_base) {
            a.push_back(intern(line));
        }
        for (const std::string_view line : m_edited) {
            b.push_back(intern(line));
        }
        // Where each base line's content sits in the base, for the copies.
        m_where.resize(m_ids.size());
        for (std::size_t i = 0; i < a.size(); ++i) {
            m_where[a[i]].push_back(static_cast<std::uint32_t>(i + 1));
        }
        m_aIds = std::move(a);
        // Each base line by the start and the end of its text, for the lines a moved and changed one may come from.
        for (std::size_t i = 0; i < m_base.size(); ++i) {
            for (const std::string_view key : keysOf(m_base[i])) {
                m_byKey[key].push_back(static_cast<std::uint32_t>(i));
            }
        }
        const auto steps = diffSequences(m_aIds, b);
        // Runs of Same become keeps; what lies between two runs is one change.
        std::size_t s = 0;
        std::uint32_t i = 0; // base lines read
        std::uint32_t j = 0; // edited lines read
        while (s < steps.size()) {
            if (steps[s] == DiffStep::Same) {
                const std::uint32_t from = i;
                while (s < steps.size() && steps[s] == DiffStep::Same) {
                    ++s;
                    ++i;
                    ++j;
                }
                m_ops.push_back(Operation{.kind = OpKind::Keep, .lines = LineRange{from + 1, i}});
                continue;
            }
            const std::uint32_t deletedFrom = i;
            const std::uint32_t insertedFrom = j;
            while (s < steps.size() && steps[s] != DiffStep::Same) {
                (steps[s] == DiffStep::Delete ? i : j) += 1;
                ++s;
            }
            change(deletedFrom, i, insertedFrom, j);
        }
        // A keep that reaches the end is written open, `= a-`, as the format's description shows.
        if (!m_ops.empty() && m_ops.back().kind == OpKind::Keep && m_ops.back().lines.last == m_base.size()) {
            m_ops.back().lines.last.reset();
        }
        return std::move(m_ops);
    }

  private:
    // The number that stands for `line`'s content.
    std::uint32_t intern(std::string_view line) {
        const auto [at, added] = m_ids.try_emplace(line, static_cast<std::uint32_t>(m_ids.size()));
        return at->second;
    }

    // Writes one change: base lines [deletedFrom, deletedTo) (from 0) give way to edited lines [insertedFrom,
    // insertedTo). Dropped lines go out before the lines that replace them, and a line paired for a rebuild consumes
    // its base line where it stands, so the base lines are consumed in order.
    void change(std::uint32_t deletedFrom, std::uint32_t deletedTo, std::uint32_t insertedFrom,
                std::uint32_t insertedTo) {
        const auto pairs = pairRebuilds(deletedFrom, deletedTo, insertedFrom, insertedTo);
        std::uint32_t consumed = deletedFrom;
        for (std::uint32_t k = insertedFrom; k < insertedTo; ++k) {
            // Drop what comes before the next rebuild's base line (or all of it when no rebuild follows).
            std::uint32_t nextRebuild = deletedTo;
            for (std::uint32_t later = k; later < insertedTo; ++later) {
                if (const auto& laterPair = pairs[later - insertedFrom]; laterPair) {
                    nextRebuild = laterPair->line;
                    break;
                }
            }
            drop(consumed, nextRebuild);
            consumed = std::max(consumed, nextRebuild);
            if (const auto& pair = pairs[k - insertedFrom]; pair) {
                m_ops.push_back(Operation{.kind = OpKind::Rebuild,
                                          .lines = LineRange{pair->line + 1, pair->line + 1},
                                          .pieces = pair->pieces});
                consumed = pair->line + 1;
            } else {
                write(m_edited[k]);
            }
        }
        drop(consumed, deletedTo);
    }

    // Drops base lines [from, to) (from 0), joined to a drop just before.
    void drop(std::uint32_t from, std::uint32_t to) {
        if (from >= to) {
            return;
        }
        if (!m_ops.empty() && m_ops.back().kind == OpKind::Drop && m_ops.back().lines.last == from) {
            m_ops.back().lines.last = to;
        } else {
            m_ops.push_back(Operation{.kind = OpKind::Drop, .lines = LineRange{from + 1, to}});
        }
        Operation& op = m_ops.back();
        op.hash = linesHash(m_base.subspan(op.lines.first - 1, to - op.lines.first + 1));
    }

    // The keys a line is found by: the first and the last bytes of its text (without its indent), so a line changed
    // in its middle or at one end still shares one with its base line. None for a line too short to be worth it.
    static std::vector<std::string_view> keysOf(std::string_view line) {
        constexpr std::size_t kKeyBytes = 6;
        const std::size_t indent = line.find_first_not_of(" \t");
        if (indent == std::string_view::npos || line.size() - indent < 2 * kKeyBytes) {
            return {};
        }
        const std::string_view text = line.substr(indent);
        return {text.substr(0, kKeyBytes), text.substr(text.size() - kKeyBytes)};
    }

    // A line built from the base line elsewhere that it keeps most of (a line moved and changed); nothing when no base
    // line sharing a key with it keeps enough. A few candidates per key are tried, so a common start costs little.
    std::optional<Operation> borrow(std::string_view line) const {
        constexpr std::size_t kCandidates = 8;
        std::optional<Operation> best;
        std::size_t bestKept = 0;
        for (const std::string_view key : keysOf(line)) {
            const auto found = m_byKey.find(key);
            if (found == m_byKey.end()) {
                continue;
            }
            for (std::size_t c = 0; c < found->second.size() && c < kCandidates; ++c) {
                const std::uint32_t from = found->second[c];
                std::size_t kept = 0;
                auto pieces = rebuildPieces(m_base[from], line, kept);
                if (pieces && kept > bestKept) {
                    best = Operation{.kind = OpKind::Borrow, .lines = LineRange{from + 1, from + 1}, .pieces = *pieces};
                    bestKept = kept;
                }
            }
        }
        return best;
    }

    // Writes an edited line that is no rebuild: a copy when the base has it, a line built from the base line it was
    // changed from, else the author's text.
    void write(std::string_view line) {
        const auto id = m_ids.find(line);
        const auto* where = id != m_ids.end() && id->second < m_where.size() ? &m_where[id->second] : nullptr;
        if (where == nullptr || where->empty()) {
            if (auto built = borrow(line); built) {
                m_ops.push_back(std::move(*built));
            } else {
                m_ops.push_back(Operation{.kind = OpKind::Add, .text = std::string(line)});
            }
            return;
        }
        // The line after the last copy keeps one copy range growing; otherwise the base's first such line.
        if (!m_ops.empty() && m_ops.back().kind == OpKind::Copy) {
            Operation& previous = m_ops.back();
            const std::uint32_t next = previous.lines.last.value_or(previous.lines.first) + 1;
            if (std::ranges::binary_search(*where, next)) {
                previous.lines.last = next;
                return;
            }
        }
        m_ops.push_back(Operation{.kind = OpKind::Copy, .lines = LineRange{where->front(), where->front()}});
    }

    // A rebuild of one base line (from 0): its pieces.
    struct Pairing {
        std::uint32_t line;
        std::vector<Piece> pieces;
    };

    // Pairs edited lines with the deleted base lines they keep most of, in order on both sides, so each pair can be a
    // rebuild. An edited line the base has whole is left to be a copy. Each edited line looks at a few deleted lines
    // past the last one paired, so a long change costs little.
    std::vector<std::optional<Pairing>> pairRebuilds(std::uint32_t deletedFrom, std::uint32_t deletedTo,
                                                     std::uint32_t insertedFrom, std::uint32_t insertedTo) const {
        constexpr std::uint32_t kWindow = 16;
        std::vector<std::optional<Pairing>> pairs(insertedTo - insertedFrom);
        std::uint32_t next = deletedFrom;
        for (std::uint32_t k = insertedFrom; k < insertedTo && next < deletedTo; ++k) {
            const auto id = m_ids.find(m_edited[k]);
            if (id != m_ids.end() && id->second < m_where.size() && !m_where[id->second].empty()) {
                continue;
            }
            std::optional<Pairing> best;
            std::size_t bestKept = 0;
            for (std::uint32_t d = next; d < deletedTo && d < next + kWindow; ++d) {
                std::size_t kept = 0;
                auto pieces = rebuildPieces(m_base[d], m_edited[k], kept);
                if (pieces && kept > bestKept) {
                    best = Pairing{d, std::move(*pieces)};
                    bestKept = kept;
                }
            }
            if (best) {
                next = best->line + 1;
                pairs[k - insertedFrom] = std::move(best);
            }
        }
        return pairs;
    }

    // The pieces that make `edited` from `base`: the base's columns where the two agree for a few bytes or more, the
    // author's text between. Nothing when they keep less than half of the base line, which is then better dropped.
    static std::optional<std::vector<Piece>> rebuildPieces(std::string_view base, std::string_view edited,
                                                           std::size_t& kept) {
        // Shorter runs than this are written as text: a piece of one or two bytes says nothing a reader can follow.
        constexpr std::size_t kMinRun = 3;
        // A line is short; a longer edit than this inside one is no small change.
        constexpr std::size_t kMaxLineCost = 64;
        if (base.empty()) {
            return std::nullopt;
        }
        // Bytes as unsigned numbers for the diff, so a byte above 0x7f is not sign-extended.
        std::vector<std::uint32_t> a;
        std::vector<std::uint32_t> b;
        for (const char c : base) {
            a.push_back(static_cast<unsigned char>(c));
        }
        for (const char c : edited) {
            b.push_back(static_cast<unsigned char>(c));
        }
        const auto steps = diffSequences(a, b, kMaxLineCost);
        std::vector<Piece> pieces;
        std::string text;
        kept = 0;
        std::size_t s = 0;
        std::size_t i = 0;
        std::size_t j = 0;
        while (s < steps.size()) {
            if (steps[s] != DiffStep::Same) {
                if (steps[s] == DiffStep::Insert) {
                    text.push_back(edited[j++]);
                } else {
                    ++i;
                }
                ++s;
                continue;
            }
            const std::size_t from = i;
            while (s < steps.size() && steps[s] == DiffStep::Same) {
                ++s;
                ++i;
                ++j;
            }
            if (i - from < kMinRun) {
                text += base.substr(from, i - from);
                continue;
            }
            if (!text.empty()) {
                pieces.emplace_back(std::move(text));
                text.clear();
            }
            const bool toEnd = i == base.size();
            pieces.emplace_back(ColumnRange{static_cast<std::uint32_t>(from),
                                            toEnd ? std::nullopt : std::optional(static_cast<std::uint32_t>(i - 1))});
            kept += i - from;
        }
        if (!text.empty()) {
            pieces.emplace_back(std::move(text));
        }
        if (2 * kept <= base.size() || pieces.size() > kMaxPieces) {
            return std::nullopt;
        }
        return pieces;
    }

    std::span<const std::string_view> m_base;
    std::span<const std::string_view> m_edited;
    std::unordered_map<std::string_view, std::uint32_t> m_ids;
    std::vector<std::vector<std::uint32_t>> m_where; ///< By line id: the base lines (from 1) with that content.
    std::vector<std::uint32_t> m_aIds;
    std::unordered_map<std::string_view, std::vector<std::uint32_t>> m_byKey; ///< keysOf() to base lines (from 0).
    std::vector<Operation> m_ops;
};

} // namespace

std::expected<PatchSection, PatchFailure> makePatch(std::string_view base, std::string_view edited,
                                                    const PatchHeader& header) {
    auto baseLines = textLines(base, "the base text");
    if (!baseLines) {
        return std::unexpected(std::move(baseLines.error()));
    }
    auto editedLines = textLines(edited, "the edited text");
    if (!editedLines) {
        return std::unexpected(std::move(editedLines.error()));
    }
    PatchSection section;
    section.entry = header.entry;
    section.name = header.name;
    section.chunk = header.chunk;
    section.form = header.form;
    section.base = textHash(base);
    section.result = textHash(edited);
    section.operations = Maker(*baseLines, *editedLines).run();
    return section;
}

} // namespace coney::patch
