// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/script_patch_apply.h"

#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "core/script_patch.h"

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
    case OpKind::Borrow:
        return "builds a line from";
    case OpKind::Add:
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
    auto lines = textLines(base, "the text");
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
        case OpKind::Rebuild: {
            const auto line = rebuildLine(op.pieces, range.front());
            if (!line) {
                return refuse(PatchFault::DoesNotFit,
                              std::format("the patch {} {} with columns {}, but the line has {} bytes", verb(op.kind),
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
            ++counts.borrowed;
            break;
        }
    }
    return counts;
}

} // namespace coney::patch
