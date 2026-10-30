// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/net_id.h"

namespace coney::net {

// Stubs for the tests' commit (fails-first); the next commit implements them.
void writeNetId(BitWriter& /*writer*/, NetId /*id*/) {}
std::expected<NetId, Error> readNetId(BitReader& /*reader*/) { return NetId{}; }
NetIdAllocator::NetIdAllocator(std::size_t capacity) : m_capacity(capacity) {}
std::expected<NetId, Error> NetIdAllocator::allocate() { return fail(ErrorCode::Invalid, "stub"); }
void NetIdAllocator::retire(NetId /*id*/) {}
bool NetIdAllocator::release(NetId /*id*/) { return false; }
bool NetIdAllocator::live(NetId /*id*/) const { return false; }

} // namespace coney::net
