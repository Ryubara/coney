// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <vector>

namespace coney::debug {

/// The last `capacity` samples of a value, oldest first: what a front end plots for a watched channel (stick axes,
/// steps per frame). A fixed ring, so sampling never allocates after construction.
class TimeSeries {
  public:
    /// An empty series that keeps at most `capacity` samples (at least 1).
    explicit TimeSeries(std::size_t capacity);

    /// Appends a sample, dropping the oldest when full.
    void push(float value);
    /// Samples held.
    [[nodiscard]] std::size_t size() const { return m_count; }
    /// The most samples held.
    [[nodiscard]] std::size_t capacity() const { return m_ring.size(); }
    /// Sample `index`, 0 the oldest; index must be below size() (CONEY_ASSERT).
    [[nodiscard]] float at(std::size_t index) const;
    /// The samples, oldest first.
    [[nodiscard]] std::vector<float> values() const;
    /// The newest sample; 0 when empty.
    [[nodiscard]] float latest() const { return m_count == 0 ? 0.0F : at(m_count - 1); }

  private:
    std::vector<float> m_ring;
    std::size_t m_next = 0; // where the next sample goes
    std::size_t m_count = 0;
};

} // namespace coney::debug
