// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <vector>

namespace coney::world_objects {

/// One node of the flag network: a flag and up to four linked flags (the nil handle, 0, for none). Coney keeps the
/// meaning of the original's 20-byte record, not its layout.
struct FlagNetNode {
    double flag = 0;
    std::array<double, 4> links{};
};

/// The level's flag network (`FlagNetAddLink`): the graph of flags that wandering pedestrians walk along. At most
/// kCapacity nodes; a node past it is ignored, as the original ignores it.
///
/// Research: docs/references/bindings/world.md#flagnetaddlink
class FlagNet {
  public:
    /// Nodes the network holds (records at `0x006e9440`).
    static constexpr std::size_t kCapacity = 128;

    /// `FlagNetAddLink(flag, link1, link2, link3, link4)`: adds a node; false (nothing added) when the network is full.
    /// @orig 0x002a7458 FlagNet_AddNode (unknown)
    bool add(const FlagNetNode& node);
    /// The first node of `flag`; null when the flag is not in the network.
    [[nodiscard]] const FlagNetNode* node(double flag) const;
    /// The flags linked from `flag`'s node (its non-nil links), in link order; empty when it has none.
    [[nodiscard]] std::vector<double> neighbours(double flag) const;
    /// Every node, in the order added.
    [[nodiscard]] const std::vector<FlagNetNode>& all() const { return m_nodes; }
    /// Forgets the network: the level is unloaded (`FlagNetClear`).
    void clear() { m_nodes.clear(); }

  private:
    std::vector<FlagNetNode> m_nodes;
};

} // namespace coney::world_objects
