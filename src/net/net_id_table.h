// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <map>
#include <optional>
#include <utility>

#include "core/assert.h"
#include "core/error.h"
#include "net/net_id.h"

// The table each side keeps from a NetId to its own handle for the thing, and back: on the server from the handles the
// game already has (a human's `Human*`, a brain's `Brain*`, a world object's script handle), on a client to the things
// it made for the server's ids. Coney's own (docs/guides/multiplayer.md#net-ids).

namespace coney::net {

/// A two-way table between NetIds and one kind of local handle (`Handle`: a pointer, a script handle as a double, a
/// small id), each id bound to one handle and each handle to one id. A lookup by an id of another generation than the
/// one bound answers nothing, so a stale id from a late packet never finds the slot's new thing. The default `Handle`
/// (null, 0, the script's NilHandle) is never bound.
///
/// Walking it (forEach()) goes in NetId order, never by handle, so the order is the same on every machine whatever
/// the handles' values.
/// @coney-only network play; the original has none
template <typename Handle> class NetIdTable {
  public:
    // Stubs for the tests' commit (fails-first); the next commit implements them.
    std::expected<void, Error> bind(NetId /*id*/, Handle /*handle*/) { return {}; }
    bool unbind(NetId /*id*/) { return false; }
    std::expected<NetId, Error> assign(NetIdAllocator& /*ids*/, Handle /*handle*/) { return NetId{}; }
    std::optional<NetId> retire(NetIdAllocator& /*ids*/, Handle /*handle*/) { return std::nullopt; }
    [[nodiscard]] std::optional<Handle> handleOf(NetId /*id*/) const { return std::nullopt; }
    [[nodiscard]] std::optional<NetId> idOf(const Handle& /*handle*/) const { return std::nullopt; }
    template <typename Visit> void forEach(Visit&& /*visit*/) const {}
    [[nodiscard]] std::size_t size() const { return 0; }
};

} // namespace coney::net
