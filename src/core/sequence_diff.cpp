// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/sequence_diff.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "core/assert.h"

namespace coney::patch {

namespace {

using Index = std::ptrdiff_t;

// One part of the diff still to do: `a[aLo, aHi)` against `b[bLo, bHi)`.
struct Box {
    Index aLo;
    Index aHi;
    Index bLo;
    Index bHi;
};

// A point the parts are split at: on a shortest path through the box.
struct Split {
    Index a;
    Index b;
};

// Myers' linear-space diff: marks which elements of `a` are deleted and which of `b` inserted. The parts are kept on a
// stack of boxes rather than in recursion, so a long script cannot run the call stack out.
class Differ {
  public:
    Differ(std::span<const std::uint32_t> a, std::span<const std::uint32_t> b, std::size_t maxCost)
        : m_a(a), m_b(b), m_maxCost(static_cast<Index>(maxCost)), m_deleted(a.size(), false),
          m_inserted(b.size(), false) {}

    // Marks the whole diff, then reads the marks off as steps.
    std::vector<DiffStep> run() {
        std::vector<Box> boxes{Box{0, static_cast<Index>(m_a.size()), 0, static_cast<Index>(m_b.size())}};
        while (!boxes.empty()) {
            Box box = boxes.back();
            boxes.pop_back();
            trim(box);
            if (box.aLo == box.aHi || box.bLo == box.bHi) {
                replace(box);
                continue;
            }
            const auto split = middle(box);
            // Past the cost cap, or a split that would not shrink the box (which a trimmed box never gives, but the
            // loop must end whatever happens): the part is replaced whole, a correct if longer script.
            if (!split || (split->a == box.aLo && split->b == box.bLo) ||
                (split->a == box.aHi && split->b == box.bHi)) {
                replace(box);
                continue;
            }
            boxes.push_back(Box{box.aLo, split->a, box.bLo, split->b});
            boxes.push_back(Box{split->a, box.aHi, split->b, box.bHi});
        }
        return steps();
    }

  private:
    // Drops the elements the box's two ends share: they are the same in both and cost nothing.
    void trim(Box& box) const {
        while (box.aLo < box.aHi && box.bLo < box.bHi && at(m_a, box.aLo) == at(m_b, box.bLo)) {
            ++box.aLo;
            ++box.bLo;
        }
        while (box.aLo < box.aHi && box.bLo < box.bHi && at(m_a, box.aHi - 1) == at(m_b, box.bHi - 1)) {
            --box.aHi;
            --box.bHi;
        }
    }

    // Marks every element of the box's `a` deleted and of its `b` inserted.
    void replace(const Box& box) {
        for (Index i = box.aLo; i < box.aHi; ++i) {
            m_deleted[static_cast<std::size_t>(i)] = true;
        }
        for (Index j = box.bLo; j < box.bHi; ++j) {
            m_inserted[static_cast<std::size_t>(j)] = true;
        }
    }

    // The middle snake: searches forward from the box's top-left and backward from its bottom-right, one edit at a
    // time, until the two paths meet; the meeting point is on a shortest path. Nothing past the cost cap.
    std::optional<Split> middle(const Box& box) {
        const Index n = box.aHi - box.aLo;
        const Index m = box.bHi - box.bLo;
        const Index delta = n - m;
        const bool odd = (delta & 1) != 0;
        const Index limit = std::min((n + m + 1) / 2, m_maxCost);
        const Index offset = limit + 1;
        m_forward.assign(static_cast<std::size_t>((2 * limit) + 3), 0);
        m_backward.assign(static_cast<std::size_t>((2 * limit) + 3), 0);
        // The furthest `x` reached on diagonal `k` (x - y), from the start and, in reversed coordinates, from the end.
        auto forward = [&](Index k) -> Index& { return m_forward[static_cast<std::size_t>(offset + k)]; };
        auto backward = [&](Index k) -> Index& { return m_backward[static_cast<std::size_t>(offset + k)]; };
        for (Index d = 0; d <= limit; ++d) {
            for (Index k = -d; k <= d; k += 2) {
                Index x =
                    (k == -d || (k != d && forward(k - 1) < forward(k + 1))) ? forward(k + 1) : forward(k - 1) + 1;
                Index y = x - k;
                while (x < n && y < m && at(m_a, box.aLo + x) == at(m_b, box.bLo + y)) {
                    ++x;
                    ++y;
                }
                forward(k) = x;
                const Index other = delta - k;
                if (odd && other >= -(d - 1) && other <= d - 1 && x + backward(other) >= n) {
                    return Split{box.aLo + x, box.bLo + y};
                }
            }
            for (Index k = -d; k <= d; k += 2) {
                Index x =
                    (k == -d || (k != d && backward(k - 1) < backward(k + 1))) ? backward(k + 1) : backward(k - 1) + 1;
                Index y = x - k;
                while (x < n && y < m && at(m_a, box.aHi - 1 - x) == at(m_b, box.bHi - 1 - y)) {
                    ++x;
                    ++y;
                }
                backward(k) = x;
                const Index other = delta - k;
                if (!odd && other >= -d && other <= d && x + forward(other) >= n) {
                    return Split{box.aHi - x, box.bHi - y};
                }
            }
        }
        return std::nullopt;
    }

    // Reads the marks off in order: a deleted element of `a`, then an inserted one of `b`, else one both share.
    [[nodiscard]] std::vector<DiffStep> steps() const {
        std::vector<DiffStep> out;
        out.reserve(m_a.size() + m_b.size());
        std::size_t i = 0;
        std::size_t j = 0;
        while (i < m_a.size() || j < m_b.size()) {
            if (i < m_a.size() && m_deleted[i]) {
                out.push_back(DiffStep::Delete);
                ++i;
            } else if (j < m_b.size() && m_inserted[j]) {
                out.push_back(DiffStep::Insert);
                ++j;
            } else {
                // Every element neither deleted nor inserted pairs with one on the other side, in order.
                CONEY_ASSERT(i < m_a.size() && j < m_b.size() && m_a[i] == m_b[j]);
                out.push_back(DiffStep::Same);
                ++i;
                ++j;
            }
        }
        return out;
    }

    // The element at a signed index.
    [[nodiscard]] static std::uint32_t at(std::span<const std::uint32_t> s, Index i) {
        return s[static_cast<std::size_t>(i)];
    }

    std::span<const std::uint32_t> m_a;
    std::span<const std::uint32_t> m_b;
    Index m_maxCost;
    std::vector<bool> m_deleted;
    std::vector<bool> m_inserted;
    std::vector<Index> m_forward;
    std::vector<Index> m_backward;
};

} // namespace

std::vector<DiffStep> diffSequences(std::span<const std::uint32_t> a, std::span<const std::uint32_t> b,
                                    std::size_t maxCost) {
    return Differ(a, b, maxCost).run();
}

} // namespace coney::patch
