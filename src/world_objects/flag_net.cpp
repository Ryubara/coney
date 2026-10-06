// SPDX-License-Identifier: GPL-3.0-or-later
#include "world_objects/flag_net.h"

#include <algorithm>

namespace coney::world_objects {

bool FlagNet::add(const FlagNetNode& node) {
    if (m_nodes.size() >= kCapacity) {
        return false;
    }
    m_nodes.push_back(node);
    return true;
}

const FlagNetNode* FlagNet::node(double flag) const {
    const auto found = std::ranges::find(m_nodes, flag, &FlagNetNode::flag);
    return found == m_nodes.end() ? nullptr : &*found;
}

std::vector<double> FlagNet::neighbours(double flag) const {
    std::vector<double> result;
    if (const FlagNetNode* found = node(flag); found != nullptr) {
        for (const double link : found->links) {
            if (link != 0.0) {
                result.push_back(link);
            }
        }
    }
    return result;
}

} // namespace coney::world_objects
