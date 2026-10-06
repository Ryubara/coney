// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/error.h"
#include "scenes/scene_list.h"
#include "scenes/scene_record.h"

namespace coney::scenes {

/// Reads the record `<name>.scn` (the name without the extension); fails as the file system does.
using SceneRecordSource = std::function<std::expected<std::vector<std::byte>, Error>(std::string_view name)>;

/// A scene's state (the header's `+0x1c`), with Coney's two for a slot before its record is in.
enum class SceneState : std::uint8_t {
    Empty = 0,    ///< Coney's: the slot holds nothing.
    Loading = 1,  ///< Coney's: the file is requested.
    Loaded = 2,   ///< Loaded and idle.
    Starting = 4, ///< Its task waits for everything it needs.
    Playing = 5,
    Stopping = 6, ///< Its clips are being ended (a role in a paired move).
    Ending = 7,   ///< Everything is done; the end gives it all back.
    Ended = 8,    ///< Over; the slot is unloaded on the next update.
};

/// One of the 12 scene slots (`0x006eba18`, 0x40 bytes each): the header record, the two segment buffers a long
/// scene streams through, and what the scripts have bound to its roles and objects.
struct SceneSlot {
    std::uint32_t id = 0; ///< `+0x04`'s record id.
    SceneState state = SceneState::Empty;
    std::unique_ptr<SceneHeader> header; ///< `+0x00`; null until loaded.
    /// `+0x0c`, `+0x10`: the two segment buffers and the part (1 for the first segment) each holds, 0 for none.
    std::array<std::unique_ptr<SceneSegment>, 2> buffers;
    std::array<std::size_t, 2> bufferPart{};
    /// `+0x30`: the buffer the last segment went into; nothing before the first.
    std::optional<std::size_t> current;
    /// The suffix of part k's record at k - 1: the header's first suffix, then each loaded segment's next one.
    std::vector<std::string> suffixes;
    std::uint32_t users = 0;         ///< `+0x34`: `ScenePreload` calls on the scene since it loaded.
    std::string callback;            ///< `+0x38`: the `ScenePreload` callback, called once when the record arrives.
    bool callbackDue = false;        ///< The record has arrived and the callback is still to be called.
    std::uint64_t lastRequestMs = 0; ///< `+0x3c`: for eviction.
    /// The humans bound to the roles and the objects bound to the object slots (the definitions' `+0x50`); 0 for
    /// none.
    std::vector<double> roleHandles;
    std::vector<double> objectHandles;

    /// The newest part loaded: 0 for the header alone.
    [[nodiscard]] std::size_t newestPart() const { return current ? bufferPart.at(*current) : 0; }
};

/// The 12 scene slots and the loading of records into them: `ScenePreload`'s requests, the callbacks when a record
/// arrives, a play binding's wait for its file, the segments streamed into two buffers while a long scene plays, and
/// the eviction of an idle scene when every slot is taken.
///
/// Coney's file reads are synchronous, so a requested record arrives on the next service() (the game's own file
/// manager delivers it a few frames later); a request a play binding waits for is read there and then.
///
/// Research: docs/research/scenes.md#slots, docs/research/scenes.md#loading
class SceneCache {
  public:
    /// The number of slots.
    static constexpr std::size_t kSlots = 12;

    /// A cache that finds names in `list` and reads records through `source`.
    SceneCache(const SceneList& list, SceneRecordSource source) : m_list(&list), m_source(std::move(source)) {}

    /// `ScenePreload`'s worker: a scene loaded or loading gets one more user and nothing else; otherwise a slot is
    /// taken (SceneSlot_Get's order: the slot holding the id, an empty slot, a slot whose scene has ended, the least
    /// recently requested idle or ended one, unloaded first) and the record requested, with `callback` (empty for
    /// none) to be called with the id when it arrives. Returns the slot; null when every slot plays a scene or `id`
    /// is not in the list.
    /// @orig 0x00353af0 Scene_Request (SceneCache.cpp)
    /// @orig 0x00353298 SceneSlot_Get (SceneCache.cpp)
    SceneSlot* request(std::uint32_t id, std::string_view callback, std::uint64_t nowMs);

    /// Delivers the requested records: each is decoded into its slot (state Loaded), and its callback, when it has
    /// one, is returned with the id for the caller to call. A record that fails to read or decode empties its slot and
    /// is logged.
    /// @orig 0x00352430 Scene_Loaded (SceneCache.cpp)
    /// @orig 0x003531c8 Scene_CallLoaded (SceneCache.cpp)
    std::vector<std::pair<std::string, std::uint32_t>> service();

    /// A play binding's wait: the slot holding `id`, requesting it first when no slot does and reading it now when it
    /// is still on its way; its callback is still returned by the next service(). Fails when no slot is free or the
    /// record does not load.
    /// @orig 0x00353020 Scene_WaitLoaded (SceneCache.cpp)
    std::expected<SceneSlot*, Error> loadNow(std::uint32_t id, std::uint64_t nowMs);

    /// The slot holding `id` (loading, loaded or playing); null when none does.
    [[nodiscard]] SceneSlot* find(std::uint32_t id);
    [[nodiscard]] const SceneSlot* find(std::uint32_t id) const;

    /// Frees the slot holding `id`.
    /// @orig 0x00351da0 SceneSlot_Unload (SceneCache.cpp)
    void unload(std::uint32_t id);

    /// The tracks of part `part` of the scene in `slot` (0 the header's, 1 the first segment's, ...), reading the
    /// segment into the other buffer when it is the next one and not in yet (a role's clip waiting for its file);
    /// null when the scene has no such part or it cannot be read.
    /// @orig 0x003a00b8 SceneTask_ClipDone (SceneTask.cpp)
    [[nodiscard]] const SceneTracks* part(SceneSlot& slot, std::size_t part);

    /// Whether the scene in `slot` has a part `part` (as far as the chain loaded so far tells).
    [[nodiscard]] static bool hasPart(const SceneSlot& slot, std::size_t part);

    /// Streams the next segment: toggles the buffer, frees what it held and loads the part after the newest. Does
    /// nothing at the chain's end. Returns whether a segment was loaded.
    /// @orig 0x00352c08 Scene_RequestSegment (SceneCache.cpp)
    /// @orig 0x00352788 SceneSegment_Fixup (SceneCache.cpp)
    bool requestNext(SceneSlot& slot);

    /// Restarts the segment chain of `slot` for a looping scene (slot flag `0x2000`): both buffers freed.
    void restartChain(SceneSlot& slot);

    /// Sets what is told of each header record as it arrives (the scene soundtrack's preload).
    void setOnLoaded(std::function<void(const SceneHeader&)> onLoaded) { m_onLoaded = std::move(onLoaded); }

    /// The scene list the cache finds names in.
    [[nodiscard]] const SceneList& list() const { return *m_list; }
    /// The slots.
    [[nodiscard]] const std::array<SceneSlot, kSlots>& slots() const { return m_slots; }
    /// Records read so far: headers and segments.
    [[nodiscard]] std::uint64_t headersRead() const { return m_headersRead; }
    [[nodiscard]] std::uint64_t segmentsRead() const { return m_segmentsRead; }
    /// The last failure to read or decode a record, for the log; empty when none.
    [[nodiscard]] const std::string& lastError() const { return m_lastError; }

  private:
    // Reads and decodes the header of the slot's scene; empties the slot on a failure.
    bool readHeader(SceneSlot& slot);
    // Empties `slot`.
    static void clear(SceneSlot& slot);

    const SceneList* m_list;
    SceneRecordSource m_source;
    std::function<void(const SceneHeader&)> m_onLoaded;
    std::array<SceneSlot, kSlots> m_slots;
    std::uint64_t m_headersRead = 0;
    std::uint64_t m_segmentsRead = 0;
    std::string m_lastError;
};

} // namespace coney::scenes
