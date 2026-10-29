// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/script_patch_apply.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "core/script_patch.h"
#include "core/sequence_diff.h"

namespace coney::patch {

namespace {

// A refusal with its fault.
std::unexpected<PatchFailure> refuse(PatchFault fault, std::string message) {
    return std::unexpected(PatchFailure{fault, std::move(message)});
}

// `base line 9` or `base lines 4-5`, as the refusals name a range.
std::string describe(std::uint32_t first, std::uint32_t last) {
    return first == last ? std::format("base line {}", first) : std::format("base lines {}-{}", first, last);
}

// What an operation does to its range, for a refusal: `keeps`, `drops`, `copies`, `rebuilds`.
std::string_view verb(OpKind kind) {
    switch (kind) {
    case OpKind::Keep:
        return "keeps";
    case OpKind::Drop:
        return "drops";
    case OpKind::Copy:
        return "copies";
    case OpKind::Rebuild:
        return "rebuilds";
    case OpKind::Add:
    case OpKind::Borrow:
        break;
    }
    return "adds";
}

// A rebuild's base columns, for a refusal: `0-27, 29-`.
std::string columnsText(std::span<const Piece> pieces) {
    std::string out;
    for (const Piece& piece : pieces) {
        if (const auto* columns = std::get_if<ColumnRange>(&piece)) {
            out += out.empty() ? "" : ", ";
            out += columns->last ? std::format("{}-{}", columns->first, *columns->last)
                                 : std::format("{}-", columns->first);
        }
    }
    return out;
}

// The patched text as it grows, line by line, held to the size limits so a patch of copies cannot make it huge.
class Output {
  public:
    // Adds one line; TooLarge past kMaxTextBytes or kMaxTextLines.
    std::expected<void, PatchFailure> line(std::string_view text) {
        if (m_lines == kMaxTextLines || m_text.size() + text.size() + 1 > kMaxTextBytes) {
            return refuse(PatchFault::TooLarge, std::format("the patched text would pass the limit of {} lines or {} "
                                                            "bytes",
                                                            kMaxTextLines, kMaxTextBytes));
        }
        if (m_lines > 0) {
            m_text.push_back('\n');
        }
        m_text += text;
        ++m_lines;
        return {};
    }
    // The text so far; the object is spent.
    std::string take() { return std::move(m_text); }

  private:
    std::string m_text;
    std::size_t m_lines = 0;
};

// A base text's lines, checked against the limits that every text is held to.
std::expected<std::vector<std::string_view>, PatchFailure> linesOf(std::string_view text, std::string_view what) {
    if (text.size() > kMaxTextBytes) {
        return refuse(PatchFault::TooLarge, std::format("{} is larger than {} bytes", what, kMaxTextBytes));
    }
    auto lines = splitLines(text);
    if (lines.size() > kMaxTextLines) {
        return refuse(PatchFault::TooLarge, std::format("{} has more than {} lines", what, kMaxTextLines));
    }
    return lines;
}

// The steps of makePatch(): the diff read as operations, with copies and rebuilds in place of the base's text.
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
                if (pairs[later - insertedFrom]) {
                    nextRebuild = pairs[later - insertedFrom]->line;
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
        op.hash = linesHash(m_base.subspan(op.lines.first - 1, *op.lines.last - op.lines.first + 1));
    }

    // Writes an edited line that is no rebuild: a copy when the base has it, else the author's text.
    void write(std::string_view line) {
        const auto id = m_ids.find(line);
        const auto* where = id != m_ids.end() && id->second < m_where.size() ? &m_where[id->second] : nullptr;
        if (where == nullptr || where->empty()) {
            m_ops.push_back(Operation{.kind = OpKind::Add, .text = std::string(line)});
            return;
        }
        // The line after the last copy keeps one copy range growing; otherwise the base's first such line.
        if (!m_ops.empty() && m_ops.back().kind == OpKind::Copy) {
            const std::uint32_t next = *m_ops.back().lines.last + 1;
            if (std::ranges::binary_search(*where, next)) {
                m_ops.back().lines.last = next;
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
        std::vector<std::uint32_t> a(base.begin(), base.end());
        std::vector<std::uint32_t> b(edited.begin(), edited.end());
        for (auto& c : a) {
            c &= 0xffU;
        }
        for (auto& c : b) {
            c &= 0xffU;
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
    std::vector<Operation> m_ops;
};

} // namespace

std::optional<std::string> rebuildLine(std::span<const Piece> pieces, std::string_view baseLine) {
    std::string line;
    for (const Piece& piece : pieces) {
        if (const auto* text = std::get_if<std::string>(&piece)) {
            line += *text;
            continue;
        }
        const auto& columns = std::get<ColumnRange>(piece);
        const std::size_t last = columns.last ? std::size_t{*columns.last} + 1 : baseLine.size();
        if (columns.first > last || last > baseLine.size()) {
            return std::nullopt;
        }
        line += baseLine.substr(columns.first, last - columns.first);
    }
    return line;
}

std::expected<std::string, PatchFailure> applyPatch(const PatchSection& section, std::string_view base) {
    // The base first: a text that is not the patch's own is refused before anything is read from it.
    if (base.size() > kMaxTextBytes) {
        return refuse(PatchFault::TooLarge, std::format("the text is larger than {} bytes", kMaxTextBytes));
    }
    if (const Sha256Digest hash = textHash(base); hash != section.base) {
        return refuse(PatchFault::WrongBase,
                      std::format("the patch is for another text: its base is {}, this text is {}",
                                  hashText(section.base), hashText(hash)));
    }
    auto lines = linesOf(base, "the text");
    if (!lines) {
        return std::unexpected(std::move(lines.error()));
    }
    const auto count = static_cast<std::uint32_t>(lines->size());
    const std::span<const std::string_view> all(*lines);
    Output out;
    std::uint32_t cursor = 1; // the next base line to consume
    bool toEnd = false;       // a keep to the end was met
    for (const Operation& op : section.operations) {
        // Every range must be inside the base; a consuming one must also start where the last ended.
        const std::uint32_t first = op.lines.first;
        const std::uint32_t last = op.kind == OpKind::Add ? first : op.lines.last.value_or(count);
        const bool consumes = op.kind == OpKind::Keep || op.kind == OpKind::Drop || op.kind == OpKind::Rebuild;
        if (consumes && toEnd) {
            return refuse(PatchFault::DoesNotFit, std::format("the patch {} {} after keeping the rest of the text",
                                                              verb(op.kind), describe(first, last)));
        }
        if (consumes && first != cursor) {
            return refuse(PatchFault::DoesNotFit, std::format("the patch {} {} out of order: base line {} comes next",
                                                              verb(op.kind), describe(first, last), cursor));
        }
        if (op.kind != OpKind::Add && (first == 0 || last < first || last > count)) {
            return refuse(PatchFault::DoesNotFit, std::format("the patch {} {}, but the text has {} lines",
                                                              verb(op.kind), describe(first, last), count));
        }
        const auto range =
            op.kind == OpKind::Add ? std::span<const std::string_view>{} : all.subspan(first - 1, last - first + 1);
        std::expected<void, PatchFailure> wrote;
        switch (op.kind) {
        case OpKind::Keep:
        case OpKind::Copy:
            for (const std::string_view line : range) {
                if (wrote = out.line(line); !wrote) {
                    break;
                }
            }
            break;
        case OpKind::Drop:
            if (linesHash(range) != op.hash) {
                return refuse(PatchFault::DamagedRange, std::format("the patch is damaged: {} do not have the hash it "
                                                                    "gives",
                                                                    describe(first, last)));
            }
            break;
        case OpKind::Add:
            wrote = out.line(op.text);
            break;
        case OpKind::Borrow:
            break;
        case OpKind::Rebuild: {
            const auto line = rebuildLine(op.pieces, range.front());
            if (!line) {
                return refuse(PatchFault::DoesNotFit,
                              std::format("the patch rebuilds {} from columns {}, but the line has {} bytes",
                                          describe(first, first), columnsText(op.pieces), range.front().size()));
            }
            wrote = out.line(*line);
            break;
        }
        }
        if (!wrote) {
            return std::unexpected(std::move(wrote.error()));
        }
        if (consumes) {
            toEnd = !op.lines.last && op.kind == OpKind::Keep;
            cursor = last + 1;
        }
    }
    if (!toEnd && cursor <= count) {
        return refuse(PatchFault::DoesNotFit,
                      std::format("the patch leaves {} unaccounted for", describe(cursor, count)));
    }
    std::string result = out.take();
    if (const Sha256Digest hash = textHash(result); hash != section.result) {
        return refuse(PatchFault::DamagedResult,
                      std::format("the patch is damaged: its result is {}, applying it gave {}",
                                  hashText(section.result), hashText(hash)));
    }
    return result;
}

std::expected<PatchSection, PatchFailure> makePatch(std::string_view base, std::string_view edited,
                                                    const PatchHeader& header) {
    auto baseLines = linesOf(base, "the base text");
    if (!baseLines) {
        return std::unexpected(std::move(baseLines.error()));
    }
    auto editedLines = linesOf(edited, "the edited text");
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

PatchCounts countOperations(const PatchSection& section) {
    PatchCounts counts;
    for (const Operation& op : section.operations) {
        const std::size_t lines = op.lines.last ? *op.lines.last - op.lines.first + 1 : 0;
        switch (op.kind) {
        case OpKind::Keep:
            break;
        case OpKind::Drop:
            counts.dropped += lines;
            break;
        case OpKind::Add:
            ++counts.added;
            break;
        case OpKind::Copy:
            counts.copied += lines;
            break;
        case OpKind::Rebuild:
            ++counts.rebuilt;
            break;
        case OpKind::Borrow:
            break;
        }
    }
    return counts;
}

} // namespace coney::patch
