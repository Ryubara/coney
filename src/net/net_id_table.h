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
    /// Binds `id` to `handle`. Fails with Invalid when `id` is not valid, its slot is bound already (in any
    /// generation: the old one is unbound first) or `handle` has an id, since on a client the id comes from the
    /// network. A default `handle` is a programmer error (CONEY_ASSERT).
    std::expected<void, Error> bind(NetId id, Handle handle) {
        CONEY_ASSERT(handle != Handle{});
        if (!id.valid()) {
            return fail(ErrorCode::Invalid, "net id: no id to bind");
        }
        if (m_bySlot.contains(id.slot)) {
            return fail(ErrorCode::Invalid, std::format("net id: slot {} is bound already", id.slot));
        }
        if (m_byHandle.contains(handle)) {
            return fail(ErrorCode::Invalid, std::format("net id: the handle for slot {} has an id already", id.slot));
        }
        m_bySlot.emplace(id.slot, Entry{id, handle});
        m_byHandle.emplace(handle, id);
        return {};
    }

    /// Unbinds `id`; false when it is not bound (in this generation).
    bool unbind(NetId id) {
        const auto found = m_bySlot.find(id.slot);
        if (found == m_bySlot.end() || found->second.id != id) {
            return false;
        }
        m_byHandle.erase(found->second.handle);
        m_bySlot.erase(found);
        return true;
    }

    /// The server's side: the id of `handle`, allocating one from `ids` and binding it when it has none (a thing that
    /// becomes replicated). Fails as NetIdAllocator::allocate() does when no slot is left.
    std::expected<NetId, Error> assign(NetIdAllocator& ids, Handle handle) {
        if (const auto existing = idOf(handle)) {
            return *existing;
        }
        auto id = ids.allocate();
        if (!id) {
            return std::unexpected(std::move(id.error()));
        }
        const auto bound = bind(*id, handle);
        CONEY_ASSERT(bound.has_value()); // a slot the allocator gives is free here too, when only it fills the table
        return *id;
    }

    /// The server's side: `handle`'s thing has gone. Unbinds it and retires its id in `ids`, and returns that id (which
    /// the server tells the clients is removed, and releases once they acknowledge it); nothing when it has no id.
    std::optional<NetId> retire(NetIdAllocator& ids, Handle handle) {
        const auto id = idOf(handle);
        if (!id) {
            return std::nullopt;
        }
        unbind(*id);
        ids.retire(*id);
        return id;
    }

    /// The handle bound to `id`, when `id` is bound in this generation.
    [[nodiscard]] std::optional<Handle> handleOf(NetId id) const {
        const auto found = m_bySlot.find(id.slot);
        if (found == m_bySlot.end() || found->second.id != id) {
            return std::nullopt;
        }
        return found->second.handle;
    }

    /// The id bound to `handle`, when it has one.
    [[nodiscard]] std::optional<NetId> idOf(const Handle& handle) const {
        const auto found = m_byHandle.find(handle);
        if (found == m_byHandle.end()) {
            return std::nullopt;
        }
        return found->second;
    }

    /// Calls `visit(id, handle)` for every binding, in NetId order.
    template <typename Visit> void forEach(Visit&& visit) const {
        for (const auto& [slot, entry] : m_bySlot) {
            visit(entry.id, entry.handle);
        }
    }

    /// Bindings held.
    [[nodiscard]] std::size_t size() const { return m_bySlot.size(); }

  private:
    // One binding.
    struct Entry {
        NetId id;
        Handle handle;
    };

    std::map<std::uint16_t, Entry> m_bySlot; // by slot: one generation bound per slot, walked in slot order
    std::map<Handle, NetId> m_byHandle;      // lookups only, never walked
};

} // namespace coney::net
