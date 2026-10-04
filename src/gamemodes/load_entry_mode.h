// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "core/chunk_stacks.h"
#include "core/chunk_system.h"
#include "core/error.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode.h"

namespace coney {

/// How a WAD entry was parsed by loadWadEntry().
enum class ContainerKind : std::uint8_t {
    Flat,    ///< A flat chunk container.
    Grouped, ///< A grouped container (a pack of resources).
};

/// The result of loading one WAD entry through the chunk system.
struct EntryLoad {
    ContainerKind kind = ContainerKind::Flat;
    bool packageMarker = false;      ///< The container header carries kPackageMarker.
    chunk::LoadReport report;        ///< What was read.
    chunk::ChunkStacks stacks;       ///< What the handlers left on the stacks.
    std::uint64_t trailingBytes = 0; ///< Bytes of the entry after the container.
};

/// Loads one WAD entry through the chunk system with `table`. An entry whose header carries the package marker is
/// read as a grouped container; any other as a flat one, and if that fails, as a grouped one (some grouped
/// containers may lack the marker; docs/research/chunk-system.md#container-layout counts more entries that parse
/// grouped than packs). Fails with the flat parse's Error when neither works, which is the case for every entry that
/// is not a chunk container (Lua bytecode, text, sound banks).
[[nodiscard]] std::expected<EntryLoad, Error> loadWadEntry(const io::Wad& wad, const io::WadEntry& entry,
                                                           const chunk::ChunkHandlerTable& table);

/// A few lines describing a load: the container's shape, the chunk count and bytes per type, and what is left on
/// the stacks. Counts and sizes only, never the data. Each line ends with a newline.
[[nodiscard]] std::string describeLoad(const EntryLoad& load);

/// A game mode that loads WAD entries through the chunk system, one entry per frame, prints a summary of each and
/// leaves when all are done. It backs `coney --load` (docs/guides/building.md#run-coney).
class LoadEntryMode final : public GameMode {
  public:
    /// The mode's id, outside the original's range of ids.
    static constexpr std::uint32_t kId = 0x100;

    /// Loads each of `requests` (a name or a `0x` hash, see io::Wad::lookup()) from `wad` with `table`, and passes
    /// each summary or error line to `print`. `wad` and `table` must outlive the mode.
    LoadEntryMode(const io::Wad& wad, const chunk::ChunkHandlerTable& table, std::vector<std::string> requests,
                  std::function<void(std::string_view)> print);

    [[nodiscard]] std::uint32_t id() const override { return kId; }
    ModeResult update(GameModeStack& stack, const FrameTime& frame) override;

    /// Requests that failed (not found, or not parsed), so far.
    [[nodiscard]] int failures() const { return m_failures; }

  private:
    const io::Wad& m_wad;
    const chunk::ChunkHandlerTable& m_table;
    std::vector<std::string> m_requests;
    std::function<void(std::string_view)> m_print;
    std::size_t m_next = 0;
    int m_failures = 0;
};

} // namespace coney
