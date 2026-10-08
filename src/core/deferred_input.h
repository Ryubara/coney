// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <optional>

#include "core/pads.h"

namespace coney {

/// An input source that starts late: until `ready` first says yes it gives port 1 connected and at rest (the other
/// ports unplugged) and leaves `inner` alone; from that frame on it plays `inner` with its frames counted from 0. So a
/// pad script written for a level's first frame of play keeps its timing however long the game takes to get there
/// (`coney --play-level` with an input script: the level's loading screen and intro movie come first).
///
/// Coney's tool: the original has nothing like it.
class DeferredInput final : public InputSource {
  public:
    /// Plays `inner` (which must outlive this) from the first frame `ready` returns true for; `ready` is asked once per
    /// frame before that, as the frame is sampled.
    DeferredInput(InputSource& inner, std::function<bool()> ready);

    /// Port 1 idle until ready, then `inner`'s frame `frame` - origin().
    [[nodiscard]] PortSamples sample(std::uint64_t frame) override;

    /// The frame `inner` started on; nothing before then.
    [[nodiscard]] const std::optional<std::uint64_t>& origin() const { return m_origin; }
    /// Frames sampled since `inner` started (0 before), so 1 after its first frame.
    [[nodiscard]] std::uint64_t framesPlayed() const { return m_played; }

  private:
    InputSource& m_inner;
    std::function<bool()> m_ready;
    std::optional<std::uint64_t> m_origin;
    std::uint64_t m_played = 0;
};

} // namespace coney
