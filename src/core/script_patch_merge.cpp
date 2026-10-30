// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/script_patch_merge.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iterator>
#include <numeric>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/script_patch.h"
#include "core/script_patch_apply.h"

namespace coney::patch {

namespace {

// One change of a patch: the base lines it consumes, [first, end) numbered from 1 (`first == end` for an insertion
// before line `first`), the operations that make it and the lines it writes there.
struct Hunk {
    std::uint32_t first = 1;
    std::uint32_t end = 1;
    std::vector<Operation> ops{};
    std::vector<std::string> lines{}; ///< What it writes: two changes are the same when these and the range are.
    std::size_t origin = 0;           ///< The index, in the patches merged, of the patch it comes from.
};

// A refusal with its fault.
std::unexpected<PatchFailure> refuse(PatchFault fault, std::string message) {
    return std::unexpected(PatchFailure{fault, std::move(message)});
}

// The changes of `section`, an applied patch of the text whose lines are `base`: each run of operations between two
// keeps is one.
std::vector<Hunk> hunksOf(const PatchSection& section, std::span<const std::string_view> base, std::size_t origin) {
    std::vector<Hunk> hunks;
    bool open = false;
    std::uint32_t cursor = 1;
    const auto count = static_cast<std::uint32_t>(base.size());
    for (const Operation& op : section.operations) {
        if (op.kind == OpKind::Keep) {
            if (open) {
                hunks.back().end = cursor;
                open = false;
            }
            cursor = op.lines.last.value_or(count) + 1;
            continue;
        }
        if (!open) {
            hunks.push_back(Hunk{.first = cursor, .end = cursor, .origin = origin});
            open = true;
        }
        Hunk& hunk = hunks.back();
        hunk.ops.push_back(op);
        // The section was applied before it got here, so every range and column fits.
        switch (op.kind) {
        case OpKind::Drop:
            cursor = *op.lines.last + 1;
            break;
        case OpKind::Add:
            hunk.lines.push_back(op.text);
            break;
        case OpKind::Copy:
            for (std::uint32_t line = op.lines.first; line <= *op.lines.last; ++line) {
                hunk.lines.emplace_back(base[line - 1]);
            }
            break;
        case OpKind::Rebuild:
            hunk.lines.push_back(rebuildLine(op.pieces, base[op.lines.first - 1]).value_or(""));
            cursor = op.lines.first + 1;
            break;
        case OpKind::Borrow:
            hunk.lines.push_back(rebuildLine(op.pieces, base[op.lines.first - 1]).value_or(""));
            break;
        case OpKind::Keep:
            break;
        }
    }
    if (open) {
        hunks.back().end = cursor;
    }
    return hunks;
}

// Whether two changes make the same edit at the same place.
bool same(const Hunk& x, const Hunk& y) { return x.first == y.first && x.end == y.end && x.lines == y.lines; }

// Whether two different changes meet: they share a base line, they insert at the same place, or one inserts strictly
// inside the other's range. Ranges that only touch do not meet: exact line numbers say where each goes.
bool meet(const Hunk& x, const Hunk& y) {
    const bool xInserts = x.first == x.end;
    const bool yInserts = y.first == y.end;
    if (xInserts && yInserts) {
        return x.first == y.first;
    }
    if (xInserts) {
        return y.first < x.first && x.first < y.end;
    }
    if (yInserts) {
        return x.first < y.first && y.first < x.end;
    }
    return x.first < y.end && y.first < x.end;
}

// Finds the group each item is in, for joining changes that conflict into one place (union-find).
class Groups {
  public:
    explicit Groups(std::size_t count) : m_parent(count) { std::iota(m_parent.begin(), m_parent.end(), 0); }
    // The group's representative.
    std::size_t find(std::size_t item) {
        while (m_parent[item] != item) {
            m_parent[item] = m_parent[m_parent[item]];
            item = m_parent[item];
        }
        return item;
    }
    // Puts two items in one group.
    void join(std::size_t a, std::size_t b) { m_parent[find(a)] = find(b); }

  private:
    std::vector<std::size_t> m_parent;
};

// Merges the changes of one more patch, `later`, into those merged so far, `earlier`: changes that meet form one
// conflict per place (a group of changes joined by meeting), settled by `choose`; the rest are taken together.
std::vector<Hunk> mergeInto(std::vector<Hunk> earlier, std::vector<Hunk> later, std::size_t laterOrigin,
                            const ConflictChooser& choose, std::vector<MergeConflict>& conflicts) {
    // An identical change made by both is applied once: the later copy goes.
    std::erase_if(
        later, [&](const Hunk& y) { return std::ranges::any_of(earlier, [&](const Hunk& x) { return same(x, y); }); });
    // Items 0..earlier-1 are the earlier changes, the rest the later ones.
    const std::size_t split = earlier.size();
    Groups groups(split + later.size());
    std::vector<bool> conflicting(split + later.size(), false);
    for (std::size_t i = 0; i < split; ++i) {
        for (std::size_t j = 0; j < later.size(); ++j) {
            if (meet(earlier[i], later[j])) {
                groups.join(i, split + j);
                conflicting[i] = true;
                conflicting[split + j] = true;
            }
        }
    }
    // One conflict per group, in base order: the earlier side's range and the later side's.
    std::vector<bool> drop(split + later.size(), false);
    std::vector<std::size_t> roots;
    for (std::size_t item = 0; item < split + later.size(); ++item) {
        if (conflicting[item] && std::ranges::find(roots, groups.find(item)) == roots.end()) {
            roots.push_back(groups.find(item));
        }
    }
    std::vector<std::pair<MergeConflict, std::size_t>> places;
    for (const std::size_t root : roots) {
        MergeConflict conflict{.later = laterOrigin};
        bool seenEarlier = false;
        bool seenLater = false;
        for (std::size_t item = 0; item < split + later.size(); ++item) {
            if (!conflicting[item] || groups.find(item) != root) {
                continue;
            }
            const bool isEarlier = item < split;
            const Hunk& hunk = isEarlier ? earlier[item] : later[item - split];
            bool& seen = isEarlier ? seenEarlier : seenLater;
            std::uint32_t& first = isEarlier ? conflict.earlierFirst : conflict.laterFirst;
            std::uint32_t& end = isEarlier ? conflict.earlierEnd : conflict.laterEnd;
            if (!seen || hunk.first < first) {
                first = hunk.first;
                if (isEarlier) {
                    conflict.earlier = hunk.origin;
                }
            }
            end = seen ? std::max(end, hunk.end) : hunk.end;
            seen = true;
        }
        places.emplace_back(conflict, root);
    }
    std::ranges::sort(places, {}, [](const auto& place) { return place.first.earlierFirst; });
    // The side not chosen at each place goes.
    for (const auto& [conflict, root] : places) {
        const MergeSide keep = choose ? choose(conflict) : MergeSide::Earlier;
        for (std::size_t item = 0; item < split + later.size(); ++item) {
            if (conflicting[item] && groups.find(item) == root) {
                drop[item] = keep == MergeSide::Earlier ? item >= split : item < split;
            }
        }
        conflicts.push_back(conflict);
    }
    std::vector<Hunk> merged;
    for (std::size_t item = 0; item < split + later.size(); ++item) {
        if (!drop[item]) {
            merged.push_back(std::move(item < split ? earlier[item] : later[item - split]));
        }
    }
    // In base order; an insertion at a range's first line comes before it and one at its end after it.
    std::ranges::stable_sort(
        merged, [](const Hunk& x, const Hunk& y) { return x.first != y.first ? x.first < y.first : x.end < y.end; });
    return merged;
}

} // namespace

std::expected<MergeResult, PatchFailure> mergePatches(std::span<const PatchSection> sections, std::string_view base,
                                                      const ConflictChooser& choose) {
    if (sections.empty()) {
        return refuse(PatchFault::DoesNotFit, "there is no patch to merge");
    }
    // Every patch must be of the same text in the same form, and apply to it whole, before any is merged.
    const PatchSection& firstSection = sections.front();
    for (const PatchSection& section : sections) {
        if (section.base != firstSection.base || section.chunk != firstSection.chunk ||
            section.form != firstSection.form) {
            return refuse(PatchFault::WrongBase,
                          std::format("the patches are of different texts: one is of {} (form {}), another of {} "
                                      "(form {})",
                                      hashText(firstSection.base), firstSection.form, hashText(section.base),
                                      section.form));
        }
        if (auto applied = applyPatch(section, base); !applied) {
            return std::unexpected(std::move(applied.error()));
        }
    }
    const auto lines = splitLines(base);
    MergeResult result;
    std::vector<Hunk> merged = hunksOf(firstSection, lines, 0);
    for (std::size_t i = 1; i < sections.size(); ++i) {
        merged = mergeInto(std::move(merged), hunksOf(sections[i], lines, i), i, choose, result.conflicts);
    }
    // The merged section: the first patch's header, keeps between the changes, and the hash of the text they make.
    result.merged = firstSection;
    result.merged.operations.clear();
    std::string text;
    std::size_t written = 0;
    // Adds one line to the merged text.
    auto write = [&](std::string_view line) {
        if (written++ > 0) {
            text.push_back('\n');
        }
        text += line;
    };
    std::uint32_t cursor = 1;
    const auto count = static_cast<std::uint32_t>(lines.size());
    for (Hunk& hunk : merged) {
        if (hunk.first > cursor) {
            result.merged.operations.push_back(
                Operation{.kind = OpKind::Keep, .lines = LineRange{cursor, hunk.first - 1}});
            for (std::uint32_t line = cursor; line < hunk.first; ++line) {
                write(lines[line - 1]);
            }
        }
        for (const std::string& line : hunk.lines) {
            write(line);
        }
        std::ranges::move(hunk.ops, std::back_inserter(result.merged.operations));
        cursor = hunk.end;
    }
    if (cursor <= count) {
        result.merged.operations.push_back(Operation{.kind = OpKind::Keep, .lines = LineRange{cursor, std::nullopt}});
        for (std::uint32_t line = cursor; line <= count; ++line) {
            write(lines[line - 1]);
        }
    }
    if (text.size() > kMaxTextBytes || written > kMaxTextLines) {
        return refuse(PatchFault::TooLarge, std::format("the merged text would pass the limit of {} lines or {} bytes",
                                                        kMaxTextLines, kMaxTextBytes));
    }
    result.merged.result = textHash(text);
    return result;
}

} // namespace coney::patch
